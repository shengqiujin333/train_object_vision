#pragma once

// railhmi-core: 轻量目标跟踪（视觉推理 ITEM-008）。
//
// 在没有专用跟踪模型时提供**显式且可验证**的相机级目标关联：按标签 + IoU（可选中心距
// 回退）贪心关联前后帧，生成稳定 track_id（从 1 开始单调递增，**永不使用 0**）。
// 跟踪丢失超过 max_misses 即删除；generation 变化时 reset()，旧目标 id 不复用。
//
// 纯标准库；不含 Qt/GStreamer/rknn。

#include <cstdint>
#include <string>
#include <vector>

#include "railhmi/core/vision_inference_envelope.h"  // EnvelopeBox

namespace railhmi {
namespace core {

struct TrackerConfig {
    float iou_match_min = 0.3f;      // IoU 匹配门限
    bool center_fallback = true;     // IoU 不足时按中心距匹配（轻量兜底）
    float center_dist_max = 0.25f;   // 归一化中心距上限
    int max_misses = 3;              // 连续未匹配帧数上限（超过删除）
    int max_tracks = 32;             // 同时跟踪目标上限
    long long max_gap_ms = 1000;     // 超过该时间未更新视为过期（不匹配）
};

struct TrackState {
    std::int32_t track_id = -1;
    std::string label;
    float x = 0, y = 0, w = 0, h = 0;
    long long last_ts_ms = 0;
    int hits = 0;
    int misses = 0;
};

struct TrackedBox {
    float x = 0, y = 0, w = 0, h = 0;
    float confidence = 0;
    std::string label;
    std::int32_t track_id = -1;  // >=1；无法分配（超 max_tracks）时为 -1
    bool is_new = false;
};

// 相机级事件适配的显式关联键：null（-1），**不是 0**。
std::int32_t camera_level_track_id();

// 单相机轻量跟踪器（单线程使用）。
class LightweightTracker {
public:
    explicit LightweightTracker(TrackerConfig cfg = TrackerConfig());

    // 输入同帧检测（顺序任意），返回与输入**同序**的带 track_id 结果。
    std::vector<TrackedBox> update(const std::vector<EnvelopeBox>& boxes, long long ts_ms);

    // 无检测帧：增加 misses 并淘汰超限 track，返回淘汰数。
    int tick_missing(long long ts_ms);

    // generation 变化：清空 track；id 继续单调递增（不复用旧 id）。
    void reset();

    std::size_t size() const { return tracks_.size(); }
    const std::vector<TrackState>& tracks() const { return tracks_; }
    long long created() const { return created_; }
    long long dropped() const { return dropped_; }
    long long matched() const { return matched_; }
    long long untracked() const { return untracked_; }
    std::int32_t next_id() const { return next_id_; }

private:
    TrackerConfig cfg_;
    std::vector<TrackState> tracks_;
    std::int32_t next_id_ = 1;
    long long created_ = 0;
    long long dropped_ = 0;
    long long matched_ = 0;
    long long untracked_ = 0;
};

}  // namespace core
}  // namespace railhmi
