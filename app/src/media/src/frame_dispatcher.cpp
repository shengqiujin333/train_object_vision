#include "railhmi/media/frame_dispatcher.h"

namespace railhmi {
namespace media {

FrameDispatcher::FrameDispatcher(DispatcherConfig cfg)
    : cfg_(cfg),
      display_(cfg.display_capacity ? cfg.display_capacity : 1),
      ai_(cfg.ai_capacity ? cfg.ai_capacity : 1),
      recording_(cfg.recording_capacity ? cfg.recording_capacity : 1,
                 cfg.recording_bytes_capacity) {}

void FrameDispatcher::dispatch(const FrameLease& lease) {
    display_.push(lease, 0);
    ai_.push(lease, 0);
    const std::size_t bytes =
        lease.valid() ? static_cast<std::size_t>(lease.descriptor().layout.buffer_bytes) : 0;
    recording_.push(lease, bytes);
    std::lock_guard<std::mutex> lk(mtx_);
    ++dispatched_;
}

bool FrameDispatcher::display_latest(FrameLease* out) const { return display_.latest(out); }
bool FrameDispatcher::ai_pop(FrameLease* out) { return ai_.pop(out); }
bool FrameDispatcher::recording_pop(FrameLease* out) { return recording_.pop(out); }

std::uint64_t FrameDispatcher::dispatched() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return dispatched_;
}

std::uint64_t FrameDispatcher::display_dropped() const { return display_.dropped(); }
std::uint64_t FrameDispatcher::ai_dropped() const { return ai_.dropped(); }
std::uint64_t FrameDispatcher::recording_overflow() const { return recording_.dropped(); }
std::size_t FrameDispatcher::display_depth() const { return display_.size(); }
std::size_t FrameDispatcher::ai_depth() const { return ai_.size(); }
std::size_t FrameDispatcher::recording_depth() const { return recording_.size(); }
std::size_t FrameDispatcher::recording_bytes() const { return recording_.bytes(); }

}  // namespace media
}  // namespace railhmi
