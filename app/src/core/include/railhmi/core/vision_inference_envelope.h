#pragma once

// railhmi-core: 推理结果关联与发布（视觉推理 ITEM-006，InferenceEnvelope）。
//
// 每个结果携带 (session_id, camera_id, generation, seq) 关联键、capture/result 单调时刻、
// latency_ms（完成 − 接收，**含排队**）、availability(ready|unavailable)+reason、model_version、
// 带 track_id 的 boxes 与掩码引用。
//
//   * 旧 session / 旧 generation / 重复 seq / 乱序 seq 由 FrameGate 分别拒绝并独立计数；
//   * 结果队列有界、超载丢最旧并计数；
//   * AI 不可用 -> availability=unavailable + reason + **空框**；该语义与
//     “ready 但本次无目标（空框）”在字段上可区分（availability / reason）；
//   * track_id 未知必须是 null（-1），**禁止**用 0 冒充未知目标。
//
// 掩码为原图坐标独立二值 plane 的**零拷贝引用**：plane 指向调用方持有、在 envelope 有效期内
// 有效的缓冲（见 MaskRef 注释）。
//
// 纯标准库；不含 Qt/GStreamer/rknn。

#include <cstdint>
#include <string>
#include <vector>

#include "railhmi/core/bounded_queue.h"
#include "railhmi/core/config.h"                    // AiProfile
#include "railhmi/core/frame_identity.h"            // FrameId / FrameGate / FrameAdmission
#include "railhmi/core/vision_detect_postprocess.h"  // DetectBox

namespace railhmi {
namespace core {

enum class AiAvailability { Ready, Unavailable };
const char* to_string(AiAvailability v);

namespace envelope_reason {
inline const char* kReady() { return "READY"; }
inline const char* kAiUnavailable() { return "AI_UNAVAILABLE"; }
inline const char* kSegUnavailable() { return "SEG_UNAVAILABLE"; }
}  // namespace envelope_reason

// 结果框：在原检测框上补充 track_id（-1 = null；不得用 0 表示未知）。
struct EnvelopeBox {
    float x = 0, y = 0, w = 0, h = 0;   // 原图归一化
    float confidence = 0;
    int class_id = 0;
    std::string label;
    bool in_roi = false;
    float distance_m = -1.0f;           // -1 = 未知
    std::int32_t track_id = -1;         // -1 = null（跟踪未就绪）
};

// 掩码零拷贝引用：`plane` 必须指向调用方持有且在 envelope 有效期内有效的缓冲；
// 尺寸/stride 与原图一致，值为 0/非零。
struct MaskRef {
    int box_index = -1;                 // 与 boxes 一一对应
    int width = 0;
    int height = 0;
    int stride_bytes = 0;
    const std::vector<std::uint8_t>* plane = nullptr;
    std::string kind = "rail";
};

struct InferenceEnvelope {
    FrameId frame;                      // (session_id, camera_id, generation, seq)
    AiProfile profile = AiProfile::None;
    long long capture_mono_us = 0;
    long long result_mono_us = 0;
    int latency_ms = 0;                 // 完成 − 接收，含排队
    std::string model_version;
    AiAvailability availability = AiAvailability::Unavailable;
    std::string reason;
    std::vector<EnvelopeBox> boxes;
    std::vector<MaskRef> masks;

    bool ready() const { return availability == AiAvailability::Ready; }
    bool empty_boxes() const { return boxes.empty(); }
};

// latency = (result − capture) / 1000ms；负值（时钟异常）按 0 计（并应在调用方记录）。
int latency_ms_of(long long capture_mono_us, long long result_mono_us);

// 构造入参：零拷贝指针须在本调用期间有效。
struct EnvelopeBuildInput {
    FrameId frame;
    AiProfile profile = AiProfile::None;
    std::string model_version;
    long long capture_mono_us = 0;
    long long result_mono_us = 0;
    bool detect_ready = true;
    bool seg_ready = true;
    bool requires_segmentation = false;      // front 两阶段需要掩码
    std::string unavailable_reason;          // 覆盖默认 token（可空）
    const std::vector<DetectBox>* boxes = nullptr;
    const std::vector<MaskRef>* masks = nullptr;
    std::vector<std::int32_t> track_ids;     // 与 boxes 对齐；空 => 全 null
    std::vector<float> distances_m;          // 可空
    std::vector<char> in_roi_flags;          // 可空
};

// 按可用性构造 envelope：不可用 -> unavailable + reason + 空框；可用 -> ready + boxes/masks。
InferenceEnvelope build_envelope(const EnvelopeBuildInput& in);

// 直接构造不可用 envelope（无框）。
InferenceEnvelope make_unavailable_envelope(const FrameId& frame, AiProfile profile,
                                            const std::string& model_version,
                                            long long capture_mono_us, long long result_mono_us,
                                            const std::string& reason);

// 发布统计（逐类拒绝独立计数，可观察）。
struct EnvelopeStats {
    long long submitted = 0;
    long long accepted = 0;
    long long rejected = 0;
    long long stale_session = 0;
    long long stale_generation = 0;
    long long duplicate_seq = 0;
    long long out_of_order_seq = 0;
    long long queued = 0;
    long long queue_dropped = 0;
    long long popped = 0;
    long long unavailable = 0;
    long long empty_boxes = 0;
    long long non_empty = 0;
};

// 身份接纳（FrameGate）+ 有界丢最旧队列。
class EnvelopePublisher {
public:
    EnvelopePublisher(std::string session_id, int queue_depth);

    // 接纳 + 入队；被拒时返回 false 并给出 reason（admission 名称）。队列满丢最旧不返回 false。
    bool submit(const InferenceEnvelope& e, std::string* reject_reason = nullptr);
    bool pop(InferenceEnvelope* out);

    EnvelopeStats stats() const { return stats_; }
    std::size_t queue_size() const { return queue_.size(); }
    std::size_t queue_capacity() const { return queue_.capacity(); }
    std::uint64_t queue_dropped() const { return queue_.dropped(); }

private:
    FrameGate gate_;
    BoundedQueue<InferenceEnvelope> queue_;
    EnvelopeStats stats_;
};

// JSON（证据/诊断用）：恒含 identity/availability/reason/boxes/masks/latency。
std::string envelope_json(const InferenceEnvelope& e);
std::string envelope_stats_json(const EnvelopeStats& s);

}  // namespace core
}  // namespace railhmi
