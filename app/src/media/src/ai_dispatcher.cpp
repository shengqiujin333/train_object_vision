#include "railhmi/media/ai_dispatcher.h"

#include <utility>

#include "railhmi/core/inference.h"

namespace railhmi {
namespace media {

AiFrameRouter::AiFrameRouter(std::vector<AiCameraSpec> specs, int inference_interval,
                             std::size_t queue_depth)
    : interval_(inference_interval < 1 ? 1 : inference_interval),
      depth_(queue_depth < 1 ? 1 : queue_depth) {
    channels_.reserve(specs.size());
    ids_.reserve(specs.size());
    for (auto& s : specs) {
        auto ch = std::unique_ptr<Channel>(new Channel());
        ch->camera_id = s.camera_id;
        ch->profile = s.ai_profile;
        // 仅 ai_enabled 且 ai_profile != none 的相机进入推理队列。
        ch->enabled = s.ai_enabled && s.ai_profile != "none";
        if (ch->enabled) {
            ch->queue.reset(new core::BoundedQueue<AiFrame>(depth_));
        }
        ids_.push_back(s.camera_id);
        channels_.push_back(std::move(ch));
    }
}

AiFrameRouter::Channel* AiFrameRouter::find(const std::string& camera_id) const {
    for (const auto& c : channels_) {
        if (c->camera_id == camera_id) return c.get();
    }
    return nullptr;
}

void AiFrameRouter::on_frame(const FrameLease& lease) {
    if (!lease.valid()) return;
    const FrameDescriptor& d = lease.descriptor();
    Channel* c = find(d.camera_id);
    if (!c) return;
    std::lock_guard<std::mutex> lk(c->mtx);
    if (!c->enabled) {
        ++c->rejected;
        return;
    }
    const std::uint64_t idx = c->seen++;
    (void)idx;
    // 降频：复用 core 既有约定 app.ai.inference_interval -> frame_due(seq, interval)。
    if (!core::frame_due(d.seq, interval_)) {
        ++c->skipped;
        return;
    }
    AiFrame f;
    f.session_id = d.session_id;
    f.camera_id = d.camera_id;
    f.profile = c->profile;
    f.generation = d.generation;
    f.seq = d.seq;
    f.capture_mono_us = d.capture_mono_us;
    f.capture_wall_ms = d.capture_wall_ms;
    f.lease = lease;
    const std::uint64_t before = c->queue->dropped();
    c->queue->push(f, 0);
    c->dropped += c->queue->dropped() - before;
    ++c->enqueued;
}

bool AiFrameRouter::pop(const std::string& camera_id, AiFrame* out) {
    Channel* c = find(camera_id);
    if (!c || !c->enabled || !c->queue) return false;
    return c->queue->pop(out);
}

bool AiFrameRouter::enabled(const std::string& camera_id) const {
    Channel* c = find(camera_id);
    return c && c->enabled;
}

std::size_t AiFrameRouter::depth(const std::string& camera_id) const {
    Channel* c = find(camera_id);
    if (!c || !c->queue) return 0;
    return c->queue->size();
}

std::uint64_t AiFrameRouter::enqueued(const std::string& camera_id) const {
    Channel* c = find(camera_id);
    if (!c) return 0;
    std::lock_guard<std::mutex> lk(c->mtx);
    return c->enqueued;
}

std::uint64_t AiFrameRouter::dropped(const std::string& camera_id) const {
    Channel* c = find(camera_id);
    if (!c) return 0;
    std::lock_guard<std::mutex> lk(c->mtx);
    return c->dropped;
}

std::uint64_t AiFrameRouter::skipped_interval(const std::string& camera_id) const {
    Channel* c = find(camera_id);
    if (!c) return 0;
    std::lock_guard<std::mutex> lk(c->mtx);
    return c->skipped;
}

std::uint64_t AiFrameRouter::rejected_disabled(const std::string& camera_id) const {
    Channel* c = find(camera_id);
    if (!c) return 0;
    std::lock_guard<std::mutex> lk(c->mtx);
    return c->rejected;
}

std::uint64_t AiFrameRouter::total_enqueued() const {
    std::uint64_t t = 0;
    for (const auto& c : channels_) {
        std::lock_guard<std::mutex> lk(c->mtx);
        t += c->enqueued;
    }
    return t;
}

}  // namespace media
}  // namespace railhmi
