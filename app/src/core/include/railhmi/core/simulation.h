#pragma once

// railhmi-core: 模拟输入与故障注入系统（ITEM-009）。
// 提供模拟帧源节拍、可重置的模拟检测序列（按 ai_profile 注入）与
// FI-01..FI-05、FI-10 软件开关，无需相机即可在模型层驱动全部状态；
// 注入参数与观测写入结构化日志（key=value）。确定性（模拟时钟），可单元测试。
//
// 仅依赖 C++ 标准库；不含 Qt/GStreamer/相机。

#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/rules.h"
#include "railhmi/core/stream_state.h"

namespace railhmi {
namespace core {

// 本工作项要求的故障注入项（TEST_DESIGN FI 目录）。
enum class FaultKind {
    StreamCut,             // FI-01 断流
    Freeze,                // FI-02 画面冻结
    AiExit,                // FI-03 AI 退出
    DiskLow,               // FI-04 磁盘不足
    PtzOutOfTolerance,     // FI-05 云台超差
    RecordingInterruption, // FI-10 录像中断
};
const char* to_string(FaultKind k);
const char* fault_id(FaultKind k);          // "FI-01" .. "FI-10"
// 解析 "01"/"1"/"fi-01"/"stream_cut"/"FI-10" 等；成功返回 true。
bool parse_fault(const std::string& token, FaultKind* out);

struct Observation {
    long long t_ms = 0;
    std::string type;      // stream_state|alarm|gap|storage|ptz|ai|injection|summary
    std::string detail;    // key=value ...
};

// 结构化日志单行格式。
std::string format_observation(const Observation& o);

struct SimulationConfig {
    std::string camera_id = "CAM-01";
    AiProfile profile = AiProfile::Front;
    long long duration_ms = 6000;
    long long step_ms = 500;
    long long fault_onset_ms = 1500;     // 故障开始
    long long fault_recovery_ms = 5000;  // 故障解除（之后恢复）；<=onset 表示不恢复
    std::vector<FaultKind> faults;

    StreamThresholds stream;
    RuleConfig rules;

    long long disk_total_bytes = 100LL * 1024 * 1024 * 1024;
    long long disk_free_bytes = 10LL * 1024 * 1024 * 1024;
    long long disk_low_threshold_bytes = 2LL * 1024 * 1024 * 1024;

    double ptz_target_zoom = 10.0;
    double ptz_actual_zoom = 10.0;
    double ptz_tolerance = 0.5;

    bool feed_detections = true;         // 是否注入检测序列（AI 退出时自动停）
};

class SimulationRunner {
public:
    explicit SimulationRunner(SimulationConfig cfg);
    // 确定性运行（模拟时钟），返回全部观测（含注入参数与汇总）。
    std::vector<Observation> run() const;
    const SimulationConfig& config() const { return cfg_; }

private:
    SimulationConfig cfg_;
};

}  // namespace core
}  // namespace railhmi
