#include "railhmi/media/recording_tap.h"

#include <utility>

namespace railhmi {
namespace media {

RecordingTap::RecordingTap(std::size_t max_bytes, int max_duration_ms)
    : ring_(max_bytes, max_duration_ms) {}

void RecordingTap::push(core::CompressedPacket p) {
    std::lock_guard<std::mutex> lk(mtx_);
    ring_.push(std::move(p));
}

void RecordingTap::clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    ring_.clear();
}

void RecordingTap::set_limits(std::size_t max_bytes, int max_duration_ms) {
    std::lock_guard<std::mutex> lk(mtx_);
    ring_.set_limits(max_bytes, max_duration_ms);
}

core::CompressedRingStats RecordingTap::stats() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return ring_.stats();
}

std::vector<core::CompressedPacket> RecordingTap::collect(int64_t from_ms, int64_t to_ms) const {
    std::lock_guard<std::mutex> lk(mtx_);
    return ring_.collect(from_ms, to_ms);
}

int64_t RecordingTap::first_keyframe_at_or_after(int64_t from_ms) const {
    std::lock_guard<std::mutex> lk(mtx_);
    return ring_.first_keyframe_at_or_after(from_ms);
}

std::size_t RecordingTap::max_bytes() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return ring_.max_bytes();
}

int RecordingTap::max_duration_ms() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return ring_.max_duration_ms();
}

void RecordingTap::set_source_status(const core::RecordingSourceStatus& s) {
    std::lock_guard<std::mutex> lk(mtx_);
    source_ = s;
}

core::RecordingSourceStatus RecordingTap::source_status() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return source_;
}

}  // namespace media
}  // namespace railhmi
