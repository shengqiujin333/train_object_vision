#pragma once

// railhmi-core: 推理工作线程调度与温度守卫（视觉推理 ITEM-009 的纯逻辑部分）。
//
// 职责边界：
//   * **温度语义**：实测（sysfs）/ 注入（显式标记，不得冒充实测）/ 不可用 三种来源严格区分；
//     毫摄氏度解析与多 zone 取最大值在此完成（读文件在 media 层，本层不碰设备）。
//   * **路数预算与温度守卫复用 `core::plan_inference`**，不重写预算数学；
//     在其上补“无饥饿”公平轮转：按 (等待轮数降序, 优先级升序, 注册顺序) 选路，
//     使 `npu_budget` 小于启用路数时低优先级职责不会长期饥饿。
//   * **决策可观察**：每轮的 run/skipped/idle、预算来源、温度样本、每路等待轮数与
//     最大连续跳过轮数全部随结果返回，便于探针/证据归档（不伪造 FPS/温度）。
//
// 纯标准库；不含 Qt/GStreamer/rknn；不读文件、不起线程。

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "railhmi/core/config.h"      // AiProfile
#include "railhmi/core/inference.h"   // AiSchedulerConfig / SchedDecision / ai_priority

namespace railhmi {
namespace core {

// 温度采样稳定 reason token（脱敏；不含路径细节以外的信息）。
namespace thermal_reason {
inline const char* kOk() { return "THERMAL_OK"; }
inline const char* kMissing() { return "THERMAL_MISSING"; }        // 采样源不存在/不可读
inline const char* kMalformed() { return "THERMAL_MALFORMED"; }    // 内容非数字
inline const char* kOutOfRange() { return "THERMAL_OUT_OF_RANGE"; }  // 物理上不可能的值
inline const char* kInjected() { return "THERMAL_INJECTED"; }      // 显式注入（非实测）
inline const char* kStale() { return "THERMAL_STALE"; }            // 样本超过最大年龄
}  // namespace thermal_reason

// 一次温度采样。`injected=true` 表示来自显式注入（测试/降级），**绝不能**被当作实测温度。
struct ThermalSample {
    bool valid = false;
    double celsius = 0.0;
    bool injected = false;
    std::string source = "unavailable";   // 采样源标签（如 thermal_zone0 / injected / unavailable）
    std::string reason = thermal_reason::kMissing();
    long long read_mono_ms = 0;           // 采样单调时刻（0 = 未采样）
    int zone_index = -1;                  // 实测 zone 序号；注入/不可用时 -1

    bool measured() const { return valid && !injected; }
};

// 物理合理范围（°C）：低于 -40 或高于 200 视为不可信（不用于守卫判断）。
struct ThermalLimits {
    double min_c = -40.0;
    double max_c = 200.0;
    long long max_age_ms = 5000;          // 样本最大年龄（0 = 不检查）
};

// 解析 sysfs 单行毫摄氏度（"45123" -> 45.123）。允许首尾空白与换行；
// 空串/非数字/多字段 -> kMalformed；超范围 -> kOutOfRange。
bool parse_thermal_millicelsius(const std::string& text, double* celsius, std::string* reason,
                                const ThermalLimits& limits = ThermalLimits());

// 多 zone 取最大值。zones 为 (zone 标签, 原始文本) 列表；跳过非法项，全部非法 -> 不可用。
ThermalSample pick_thermal_sample(const std::vector<std::pair<std::string, std::string>>& zones,
                                  long long now_mono_ms = 0,
                                  const ThermalLimits& limits = ThermalLimits());

// 显式注入样本（source=injected，reason=THERMAL_INJECTED）。
ThermalSample injected_thermal_sample(double celsius, long long now_mono_ms = 0);

// 年龄判定：超过 max_age_ms 视为过期（返回 false 并置 reason=kStale）。
bool thermal_sample_fresh(const ThermalSample& s, long long now_mono_ms, long long max_age_ms,
                          std::string* reason = nullptr);

// 状态 token：MEASURED / INJECTED / UNAVAILABLE（供 P5/证据区分“实测”与“注入”）。
const char* thermal_state_token(const ThermalSample& s);
std::string thermal_sample_json(const ThermalSample& s);

// 单路调度需求（由工作线程在每轮开始时快照；只描述“是否有可推理帧”）。
struct AiLaneDemand {
    std::string camera_id;
    AiProfile profile = AiProfile::None;
    bool has_frame = false;      // 该路有界队列中有可推理帧
    std::uint64_t seq = 0;       // 将被消费的帧序号（证据用；0 = 无）
};

// 一轮调度决策（可归档）。
struct AiScheduleRound {
    long long round = 0;
    std::vector<std::string> run;        // 本轮推理的路（按公平顺序）
    std::vector<std::string> skipped;    // 有帧但本轮被预算/温度跳过
    std::vector<std::string> idle;       // 无帧或非 AI 职责
    std::vector<std::string> priority_order;  // plan_inference 的优先顺序（预算来源）
    bool temperature_guard = false;
    bool thermal_known = false;          // 温度未知时为 false（不得当作“凉”宣称）
    int budget_configured = 0;
    int budget_effective = 0;            // 温度守卫降载后的实际路数
    ThermalSample thermal;
};

// 公平调度器（单线程使用；由工作线程每轮调用一次）。
class AiLaneScheduler {
public:
    explicit AiLaneScheduler(AiSchedulerConfig cfg);

    // 计算本轮决策。不产生副作用以外的 I/O；重复调用是可确定的（同一输入序列 -> 同一输出）。
    AiScheduleRound plan(const std::vector<AiLaneDemand>& demand, const ThermalSample& thermal);

    const AiSchedulerConfig& config() const { return cfg_; }
    void set_config(const AiSchedulerConfig& c) { cfg_ = c; }

    // 距上次运行经过的轮数（从未运行 = 当前轮次）。仅对已出现过的路有效，未知路返回 0。
    long long wait_rounds(const std::string& camera_id) const;
    // 全部路的等待轮数（证据用）。
    std::map<std::string, long long> waits() const;
    // 观测到的最大等待轮数（0 = 无）。
    long long max_wait_rounds() const { return max_wait_rounds_; }
    long long rounds() const { return round_; }

private:
    AiSchedulerConfig cfg_;
    long long round_ = 0;
    std::map<std::string, long long> last_run_round_;
    std::map<std::string, long long> seen_order_;
    long long order_seq_ = 0;
    long long max_wait_rounds_ = 0;
};

std::string schedule_round_json(const AiScheduleRound& r);

}  // namespace core
}  // namespace railhmi
