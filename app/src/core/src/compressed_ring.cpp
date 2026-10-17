#include "railhmi/core/compressed_ring.h"

#include <utility>

namespace railhmi {
namespace core {

CompressedRingBuffer::CompressedRingBuffer(std::size_t max_bytes, int max_duration_ms)
    : max_bytes_(max_bytes), max_duration_ms_(max_duration_ms) {}

void CompressedRingBuffer::set_limits(std::size_t max_bytes, int max_duration_ms) {
    max_bytes_ = max_bytes;
    max_duration_ms_ = max_duration_ms;
    // 收紧上限时立即淘汰（复用 push 的淘汰规则）。
    if (max_duration_ms_ > 0) {
        const int64_t last = q_.empty() ? -1 : q_.back().ts_ms;
        while (q_.size() > 1 && (last - q_.front().ts_ms) > max_duration_ms_) {
            bytes_ -= q_.front().data.size();
            q_.pop_front();
            ++evicted_duration_;
        }
    }
    if (max_bytes_ > 0) {
        while (!q_.empty() && bytes_ > max_bytes_) {
            bytes_ -= q_.front().data.size();
            q_.pop_front();
            ++evicted_bytes_;
        }
    }
}

std::size_t CompressedRingBuffer::push(CompressedPacket p) {
    bytes_ += p.data.size();
    q_.push_back(std::move(p));
    ++pushed_;

    std::size_t evicted = 0;
    if (max_duration_ms_ > 0) {
        const int64_t last = q_.back().ts_ms;
        while (q_.size() > 1 && (last - q_.front().ts_ms) > max_duration_ms_) {
            bytes_ -= q_.front().data.size();
            q_.pop_front();
            ++evicted_duration_;
            ++evicted;
        }
    }
    if (max_bytes_ > 0) {
        while (!q_.empty() && bytes_ > max_bytes_) {
            bytes_ -= q_.front().data.size();
            q_.pop_front();
            ++evicted_bytes_;
            ++evicted;
        }
    }
    return evicted;
}

void CompressedRingBuffer::clear() {
    q_.clear();
    bytes_ = 0;
}

CompressedRingStats CompressedRingBuffer::stats() const {
    CompressedRingStats s;
    s.packets = q_.size();
    s.bytes = bytes_;
    s.first_ts_ms = first_ts_ms();
    s.last_ts_ms = last_ts_ms();
    s.span_ms = span_ms();
    s.pushed = pushed_;
    s.evicted_duration = evicted_duration_;
    s.evicted_bytes = evicted_bytes_;
    for (const auto& p : q_) {
        if (p.keyframe) ++s.keyframes;
    }
    return s;
}

std::vector<CompressedPacket> CompressedRingBuffer::collect(int64_t from_ms, int64_t to_ms) const {
    std::vector<CompressedPacket> out;
    for (const auto& p : q_) {
        if (p.ts_ms >= from_ms && p.ts_ms <= to_ms) out.push_back(p);
    }
    return out;
}

int64_t CompressedRingBuffer::first_keyframe_at_or_after(int64_t from_ms) const {
    for (const auto& p : q_) {
        if (p.ts_ms >= from_ms && p.keyframe) return p.ts_ms;
    }
    return -1;
}

}  // namespace core
}  // namespace railhmi
