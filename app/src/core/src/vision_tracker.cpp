// railhmi-core: 轻量目标跟踪实现（视觉推理 ITEM-008）。仅标准库。

#include "railhmi/core/vision_tracker.h"

#include <algorithm>
#include <cmath>

namespace railhmi {
namespace core {

namespace {

float iou(const TrackState& t, const EnvelopeBox& b) {
    const float x1 = std::max(t.x, b.x);
    const float y1 = std::max(t.y, b.y);
    const float x2 = std::min(t.x + t.w, b.x + b.w);
    const float y2 = std::min(t.y + t.h, b.y + b.h);
    const float w = x2 - x1;
    const float h = y2 - y1;
    if (w <= 0.f || h <= 0.f) return 0.f;
    const float inter = w * h;
    const float uni = t.w * t.h + b.w * b.h - inter;
    return uni > 0.f ? inter / uni : 0.f;
}

float center_dist(const TrackState& t, const EnvelopeBox& b) {
    const float cx1 = t.x + t.w / 2.f, cy1 = t.y + t.h / 2.f;
    const float cx2 = b.x + b.w / 2.f, cy2 = b.y + b.h / 2.f;
    return std::sqrt((cx1 - cx2) * (cx1 - cx2) + (cy1 - cy2) * (cy1 - cy2));
}

}  // namespace

std::int32_t camera_level_track_id() { return -1; }

LightweightTracker::LightweightTracker(TrackerConfig cfg) : cfg_(cfg) {}

void LightweightTracker::reset() {
    tracks_.clear();
    // next_id_ 不重置：旧 generation 的 id 绝不复用。
}

int LightweightTracker::tick_missing(long long ts_ms) {
    int dropped = 0;
    for (auto it = tracks_.begin(); it != tracks_.end();) {
        if (ts_ms - it->last_ts_ms > cfg_.max_gap_ms) {
            it = tracks_.erase(it);
            ++dropped;
            continue;
        }
        ++it->misses;
        if (it->misses > cfg_.max_misses) {
            it = tracks_.erase(it);
            ++dropped;
        } else {
            ++it;
        }
    }
    dropped_ += dropped;
    return dropped;
}

std::vector<TrackedBox> LightweightTracker::update(const std::vector<EnvelopeBox>& boxes,
                                                   long long ts_ms) {
    // 1) 过期 track 不参与匹配（时间鸿沟视为不连续）。
    for (auto it = tracks_.begin(); it != tracks_.end();) {
        if (ts_ms - it->last_ts_ms > cfg_.max_gap_ms) {
            it = tracks_.erase(it);
            ++dropped_;
        } else {
            ++it;
        }
    }

    std::vector<TrackedBox> out(boxes.size());
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        out[i].x = boxes[i].x;
        out[i].y = boxes[i].y;
        out[i].w = boxes[i].w;
        out[i].h = boxes[i].h;
        out[i].confidence = boxes[i].confidence;
        out[i].label = boxes[i].label;
    }
    std::vector<char> box_used(boxes.size(), 0);
    std::vector<char> track_used(tracks_.size(), 0);

    // 2) 贪心匹配（同标签 + IoU；可选中心距回退），按 IoU 从高到低选择。
    struct Pair {
        float score;
        std::size_t ti;
        std::size_t bi;
    };
    std::vector<Pair> pairs;
    for (std::size_t ti = 0; ti < tracks_.size(); ++ti) {
        for (std::size_t bi = 0; bi < boxes.size(); ++bi) {
            if (tracks_[ti].label != boxes[bi].label) continue;
            const float v = iou(tracks_[ti], boxes[bi]);
            if (v >= cfg_.iou_match_min) pairs.push_back(Pair{v, ti, bi});
        }
    }
    std::stable_sort(pairs.begin(), pairs.end(),
                     [](const Pair& a, const Pair& b) { return a.score > b.score; });
    for (const Pair& p : pairs) {
        if (track_used[p.ti] || box_used[p.bi]) continue;
        TrackState& t = tracks_[p.ti];
        const EnvelopeBox& b = boxes[p.bi];
        t.x = b.x; t.y = b.y; t.w = b.w; t.h = b.h;
        t.last_ts_ms = ts_ms;
        t.hits += 1;
        t.misses = 0;
        track_used[p.ti] = 1;
        box_used[p.bi] = 1;
        out[p.bi].track_id = t.track_id;
        ++matched_;
    }
    if (cfg_.center_fallback) {
        for (std::size_t ti = 0; ti < tracks_.size(); ++ti) {
            if (track_used[ti]) continue;
            float best = cfg_.center_dist_max;
            std::size_t best_bi = boxes.size();
            for (std::size_t bi = 0; bi < boxes.size(); ++bi) {
                if (box_used[bi]) continue;
                if (tracks_[ti].label != boxes[bi].label) continue;
                const float d = center_dist(tracks_[ti], boxes[bi]);
                if (d <= best) {
                    best = d;
                    best_bi = bi;
                }
            }
            if (best_bi == boxes.size()) continue;
            TrackState& t = tracks_[ti];
            const EnvelopeBox& b = boxes[best_bi];
            t.x = b.x; t.y = b.y; t.w = b.w; t.h = b.h;
            t.last_ts_ms = ts_ms;
            t.hits += 1;
            t.misses = 0;
            track_used[ti] = 1;
            box_used[best_bi] = 1;
            out[best_bi].track_id = t.track_id;
            ++matched_;
        }
    }

    // 3) 未匹配 track：misses++，超限删除（重建保留列表，避免边遍历边删除）。
    {
        std::vector<TrackState> kept;
        kept.reserve(tracks_.size());
        for (std::size_t ti = 0; ti < tracks_.size(); ++ti) {
            TrackState t = tracks_[ti];
            if (!track_used[ti]) {
                ++t.misses;
                if (t.misses > cfg_.max_misses) {
                    ++dropped_;
                    continue;
                }
            }
            kept.push_back(std::move(t));
        }
        tracks_.swap(kept);
    }

    // 4) 未匹配检测：新建 track（受 max_tracks 限制）；id 从 1 单调递增，永不 0。
    for (std::size_t bi = 0; bi < boxes.size(); ++bi) {
        if (box_used[bi]) continue;
        if (static_cast<int>(tracks_.size()) >= cfg_.max_tracks) {
            out[bi].track_id = -1;  // 超上限：显式 untracked，不伪造 id
            ++untracked_;
            continue;
        }
        TrackState t;
        t.track_id = next_id_++;
        t.label = boxes[bi].label;
        t.x = boxes[bi].x;
        t.y = boxes[bi].y;
        t.w = boxes[bi].w;
        t.h = boxes[bi].h;
        t.last_ts_ms = ts_ms;
        t.hits = 1;
        t.misses = 0;
        tracks_.push_back(t);
        out[bi].track_id = t.track_id;
        out[bi].is_new = true;
        ++created_;
    }
    return out;
}

}  // namespace core
}  // namespace railhmi
