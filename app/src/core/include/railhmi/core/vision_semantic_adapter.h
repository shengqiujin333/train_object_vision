#pragma once

// railhmi-core: 语义适配与规则接入（视觉推理 ITEM-007，front）。
//
// 把 `InferenceEnvelope` 的框按**受支持标签白名单**严格映射为 `core::Detection`，再交给
// `RuleEngine`。关键约束：
//   * 未知 label 被拦截（不进入规则），计数可观察；
//   * 同帧多目标先按 track 聚合（同 track 取最高分），再按稳定 track_id 升序排序；
//   * **禁止**把 track_id 未知的目标映射为 0：`require_track=true` 时无 track 的框被丢弃并计数
//     （跟踪由 ITEM-008 提供；未就绪时宁可不报警也不合并目标）；
//   * 非候选目标（不在轨道内/置信度不足）只**按 track** 清理自己的确认状态，
//     不调用会整路清除同相机确认的路径，因此不会误清其他目标；
//   * generation 变化时按相机做**受控清理**（`RuleEngine::invalidate_camera`），不删除事件；
//     失帧/目标消失时按 track 清理（`RuleEngine::invalidate_track`）。
//
// 纯标准库；不含 Qt/GStreamer/rknn。

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "railhmi/core/config.h"                     // AiProfile
#include "railhmi/core/rules.h"                      // Detection / RuleEngine / AlarmEvent
#include "railhmi/core/vision_inference_envelope.h"  // InferenceEnvelope

namespace railhmi {
namespace core {

namespace semantic_reason {
inline const char* kOk() { return "SEMANTIC_OK"; }
inline const char* kNoCandidate() { return "SEMANTIC_NO_CANDIDATE"; }
inline const char* kAllBlocked() { return "SEMANTIC_ALL_BLOCKED"; }
inline const char* kUnavailable() { return "SEMANTIC_UNAVAILABLE"; }
inline const char* kStaleGeneration() { return "SEMANTIC_STALE_GENERATION"; }
inline const char* kBadConfig() { return "SEMANTIC_BAD_CONFIG"; }
}  // namespace semantic_reason

struct SemanticConfig {
    AiProfile profile = AiProfile::Front;
    std::vector<std::string> allowed_labels = {"person", "obstacle"};  // 受支持（白名单）
    std::vector<std::string> known_labels = {"person", "obstacle"};    // 模型已知标签
    double confidence_min = 0.5;          // 与 thresholds.confidence_min 一致
    bool require_track = true;            // 无 track 的框丢弃（禁止 0 冒充）
    bool use_envelope_in_roi = true;      // in_roi 由 ITEM-005 两阶段判定给出
    std::string threshold_source;         // 证据
    std::string allowed_labels_source;    // 证据
};

struct SemanticStats {
    long long input_boxes = 0;
    long long mapped = 0;                  // 通过白名单并聚合后的目标数
    long long candidates = 0;              // 满足 in_roi && confidence（会下发规则）
    long long blocked_unknown_label = 0;
    long long blocked_not_allowed = 0;
    long long dropped_no_track = 0;        // track 未就绪（禁止 0 冒充）
    long long dropped_low_confidence = 0;  // 在轨但置信度不足（不下发）
    long long dropped_off_rail = 0;        // 不在轨道掩码内（不下发）
    long long aggregated = 0;              // 同 track 重复框被合并
    long long generation_reset_keys = 0;   // generation 变化清理的 key 数
    long long track_reset_keys = 0;        // 非候选目标按 track 清理的 key 数
};

struct SemanticResult {
    bool ok = false;              // 映射过程有效（不代表有报警）
    bool available = false;       // envelope 可用（不可用不产生任何报警）
    std::string reason;
    std::string detail;
    std::vector<Detection> detections;  // 已聚合、按 track_id 升序（无 null track）
    std::vector<char> candidate;        // 与 detections 对齐：1=满足候选条件（下发规则）
    SemanticStats stats;
};

class SemanticAdapter {
public:
    explicit SemanticAdapter(SemanticConfig cfg);

    // 纯映射（不调用规则）。
    SemanticResult adapt(const InferenceEnvelope& e, long long ts_ms);

    // 下发：候选 -> RuleEngine::on_detection；非候选 -> 按 track 受控清理。
    std::vector<AlarmEvent> dispatch(SemanticResult* r, RuleEngine* rules);

    // 映射 + generation 受控清理 + 下发。
    std::vector<AlarmEvent> adapt_and_dispatch(const InferenceEnvelope& e, long long ts_ms,
                                               RuleEngine* rules, SemanticResult* out = nullptr);

    // 受控清理辅助（失帧/目标消失时由调用方按 track 调用）。
    static int invalidate_track(RuleEngine* rules, const std::string& camera_id, int track_id);
    static int invalidate_generation(RuleEngine* rules, const std::string& camera_id);

    const SemanticConfig& config() const { return cfg_; }

private:
    void note_generation(const FrameId& frame, RuleEngine* rules, SemanticResult* r) const;

    SemanticConfig cfg_;
    mutable std::map<std::string, std::uint64_t> last_generation_;
};

}  // namespace core
}  // namespace railhmi
