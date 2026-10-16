#pragma once

// railhmi-core: 推理服务模型（ITEM-017 / DESKTOP §8.1/§8.2、INTERACTION §5.5/§7.4）。
// 只做调度/队列/遥测/调试视图与“AI 不可用”语义，不处理像素、不绑定具体模型运行时
// （模型/后处理链路由视觉/模型职责提供；应用消费 模型版本/框/ROI/置信度/耗时）。
// 纯逻辑 + 显式单调时钟，可用单元测试驱动。

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "railhmi/core/camera_registry.h"
#include "railhmi/core/config.h"
#include "railhmi/core/status.h"

namespace railhmi {
namespace core {

// 检测框（归一化 [0,1]，相对原帧）。
struct DetectionBox {
    float x = 0, y = 0, w = 0, h = 0;
    float confidence = 0;
    int class_id = 0;
    std::string label;
    bool in_roi = false;         // 是否落在职责 ROI 内
    float distance_m = -1.0f;    // 目标距离（-1=未知；PRD §4.2）
};

// 一次推理结果（相机/职责/时间/耗时/版本/框）。
struct DetectionResult {
    std::string camera_id;
    AiProfile profile = AiProfile::None;
    long long ts_ms = 0;         // 结果单调时间
    int latency_ms = 0;          // 端到端耗时（取流→结果）
    std::string model_version;
    std::vector<DetectionBox> boxes;
    float max_confidence = 0;
};

// 有界丢旧帧队列（深度固定；满时丢最旧并计数）。
struct FrameRef {
    std::string camera_id;
    uint64_t seq = 0;
    long long ts_ms = 0;
};

class BoundedFrameQueue {
public:
    explicit BoundedFrameQueue(int depth);
    bool push(const FrameRef& f);        // true 表示发生了丢旧帧
    bool pop(FrameRef* out);
    std::size_t size() const { return q_.size(); }
    int depth() const { return depth_; }
    long long dropped() const { return dropped_; }
    void clear() { q_.clear(); }

private:
    int depth_;
    std::deque<FrameRef> q_;
    long long dropped_ = 0;
};

// 优先级：front(1) > wheel_rail(2) > pantograph(3) > cab_door(4)；none 不排队。
int ai_priority(AiProfile p);
bool ai_eligible(AiProfile p);                      // ai_profile != none
std::string ai_profile_label(AiProfile p);          // 职责文字（复用检测职责徽标）
// 每 N 帧推理一次（沿用 demo inference_interval）。
bool frame_due(uint64_t seq, int interval);

// 每路 AI 运行时。
struct AiChannelRuntime {
    std::string camera_id;
    AiProfile profile = AiProfile::None;
    std::string model_version;
    double fps = 0;
    int latency_ms = 0;
    long long last_result_ms = -1;      // -1 = 无结果
    long long last_frame_ms = -1;
    long long dropped = 0;
    int queue_depth = 0;
};

// 推理服务聚合状态（worker_alive=false 表示推理线程已停/退出）。
struct AiRuntime {
    bool worker_alive = false;
    std::string engine = "rknn";
    std::vector<AiChannelRuntime> channels;
};

// 感知可用/降级：worker 停 -> Unavailable；有路结果陈旧或超温降载 -> Warning（降级）；否则 Ok。
StatusLevel ai_status_level(const AiRuntime& rt, long long now_ms, int stale_ms = 3000);
std::string ai_status_text(const AiRuntime& rt, long long now_ms, int stale_ms = 3000);
// 供 ITEM-016 状态区/AI 项使用。
CategoryStatus ai_status_category(const AiRuntime& rt, long long now_ms, int stale_ms = 3000);
// 相关检测类报警的标注：AI 不可用时不得静默（返回“AI 不可用”，否则空串）。
std::string ai_alarm_annotation(bool ai_available);

// 调度输入/决策。
struct AiSchedulerConfig {
    int inference_interval = 5;
    int queue_depth = 2;
    int npu_budget = 4;
    double temp_limit_c = 85.0;
};
struct SchedInput {
    std::vector<AiChannelRuntime> channels;   // 顺序 = 注册表；仅 ai_eligible 参与
    double temp_c = 0.0;
    bool worker_alive = true;
};
struct SchedDecision {
    std::vector<std::string> run;             // 本轮推理的相机 id（按优先级排序）
    std::vector<std::string> skipped;         // 因预算/温度被跳过
    bool temperature_guard = false;           // 触发热降载（仍运行，不静默停）
};
SchedDecision plan_inference(const SchedInput& in, const AiSchedulerConfig& cfg);

// AI 调试页每路视图（仅 ai_enabled 相机）。
struct AiDebugChannel {
    std::string camera_id;
    std::string name;
    AiProfile profile = AiProfile::None;
    std::string profile_label;
    std::string model_version;
    bool available = false;               // worker 存活且该路有结果
    std::string unavailable_reason;       // 例如 “AI 不可用”
    double fps = 0;
    int latency_ms = 0;
    long long last_result_age_ms = -1;    // -1 = 无
    std::string last_result_text;         // “N 秒前”/“—”
    std::vector<DetectionBox> boxes;      // 最近结果框
    float max_confidence = 0;
    std::string distance_text;            // 目标距离（HLT-004/PRD §4.2）
};
std::vector<AiDebugChannel> build_ai_debug(const std::vector<CameraSlot>& registry,
                                           const AiRuntime& rt,
                                           const std::vector<DetectionResult>& latest,
                                           long long now_ms,
                                           int stale_ms = 3000);

// AI 调试页/调试叠加的可见性（司机不可见，与导航权限一致）。
bool ai_debug_visible(Role role);

}  // namespace core
}  // namespace railhmi
