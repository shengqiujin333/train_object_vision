#include "railhmi/core/clip.h"

#include <algorithm>
#include <cstdio>

namespace railhmi {
namespace core {

RollingBuffer::RollingBuffer(std::size_t capacity) : cap_(capacity) {}

void RollingBuffer::set_capacity(std::size_t capacity) { cap_ = capacity; while (q_.size() > cap_) q_.pop_front(); }

void RollingBuffer::push(long long ts_ms) {
    q_.push_back(ts_ms);
    while (cap_ > 0 && q_.size() > cap_) q_.pop_front();
}

void RollingBuffer::drop_before(long long ts_ms) {
    while (!q_.empty() && q_.front() < ts_ms) q_.pop_front();
}

std::size_t RollingBuffer::count_in(long long t0_ms, long long t1_ms) const {
    std::size_t n = 0;
    for (long long t : q_)
        if (t >= t0_ms && t <= t1_ms) ++n;
    return n;
}

const char* to_string(ClipState s) { return s == ClipState::Finalized ? "finalized" : "recording"; }

std::string clip_manifest_json(const ProtectedClip& c) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "{\"id\":%lld,\"cam\":\"%s\",\"level\":\"%s\",\"trigger_ms\":%lld,"
                  "\"t0_ms\":%lld,\"t1_ms\":%lld,\"duration_ms\":%lld,\"state\":\"%s\","
                  "\"complete\":%s,\"frame_count\":%d}",
                  c.id, c.cam.c_str(), to_string(c.level), c.trigger_ms, c.t0_ms, c.t1_ms,
                  c.duration_ms(), to_string(c.state), c.complete ? "true" : "false", c.frame_count);
    return std::string(buf);
}

long long estimate_clip_size(const ProtectedClip& clip, double mbps) {
    const double seconds = clip.duration_ms() / 1000.0;
    return static_cast<long long>(seconds * mbps * 1000000.0 / 8.0);
}

ClipRecorder::ClipRecorder(ClipConfig cfg, RetentionPolicy policy) : cfg_(cfg), policy_(policy) {
    if (cfg_.max_frames_per_channel == 0) {
        const double secs = cfg_.pre_seconds + cfg_.post_seconds;
        cfg_.max_frames_per_channel = static_cast<std::size_t>(secs * cfg_.fps) + 25;
    }
}

RollingBuffer* ClipRecorder::buf_for(const std::string& cam, bool create) {
    for (auto& p : buffers_)
        if (p.first == cam) return &p.second;
    if (!create) return nullptr;
    buffers_.emplace_back(cam, RollingBuffer(cfg_.max_frames_per_channel));
    return &buffers_.back().second;
}

RollingBuffer* ClipRecorder::buffer(const std::string& cam) { return buf_for(cam, false); }

void ClipRecorder::on_frame(const std::string& cam, long long ts_ms) {
    RollingBuffer* b = buf_for(cam, true);
    b->push(ts_ms);
    b->drop_before(ts_ms - static_cast<long long>(cfg_.pre_seconds + cfg_.post_seconds) * 1000);
}

ProtectedClip ClipRecorder::on_alarm(const std::string& cam, AlarmLevel level, long long trigger_ms) {
    ProtectedClip c;
    c.id = next_id_++;
    c.cam = cam;
    c.level = level;
    c.trigger_ms = trigger_ms;
    c.t0_ms = trigger_ms - static_cast<long long>(cfg_.pre_seconds) * 1000;
    c.t1_ms = trigger_ms + static_cast<long long>(cfg_.post_seconds) * 1000;
    c.state = ClipState::Recording;
    c.complete = false;
    clips_.push_back(c);
    return c;
}

std::size_t ClipRecorder::finalize(long long now_ms) {    std::size_t n = 0;
    for (auto& c : clips_) {
        if (c.state == ClipState::Finalized) continue;
        if (now_ms < c.t1_ms) continue;
        c.state = ClipState::Finalized;
        c.complete = true;
        RollingBuffer* b = buf_for(c.cam, false);
        c.frame_count = b ? static_cast<int>(b->count_in(c.t0_ms, c.t1_ms)) : 0;
        ++n;
    }
    return n;
}

void ClipRecorder::on_recording_interrupt(const std::string& cam, long long t0_ms, long long t1_ms,
                                          const std::string& reason) {
    add_gap(gaps_, cam, t0_ms, t1_ms, reason);
}

bool ClipRecorder::set_clip_size(long long id, long long size_bytes) {
    for (auto& c : clips_)
        if (c.id == id) { c.size_bytes = size_bytes; return true; }
    return false;
}

std::vector<RecordingSegmentRow> ClipRecorder::segments() const {
    std::vector<RecordingSegmentRow> out = normal_;
    for (const auto& c : clips_) {
        RecordingSegmentRow r;
        r.id = c.id;
        r.cam = c.cam;
        r.t0_ms = c.t0_ms;
        r.t1_ms = c.t1_ms;
        r.kind = SegmentKind::Critical;
        r.complete = c.complete;
        r.size_bytes = c.size_bytes;
        out.push_back(r);
    }
    return out;
}

void ClipRecorder::add_normal_segment(long long id, const std::string& cam, long long t0_ms,
                                      long long t1_ms, long long size_bytes) {
    RecordingSegmentRow r;
    r.id = id;
    r.cam = cam;
    r.t0_ms = t0_ms;
    r.t1_ms = t1_ms;
    r.kind = SegmentKind::Continuous;
    r.complete = true;
    r.size_bytes = size_bytes;
    normal_.push_back(r);
}

RetentionResult ClipRecorder::apply_retention_now(long long now_ms) {
    const std::vector<RecordingSegmentRow> segs = segments();
    const RetentionResult res = apply_retention(segs, now_ms, policy_);
    if (res.evicted_ids.empty()) return res;
    auto evicted = [&](long long id) {
        return std::find(res.evicted_ids.begin(), res.evicted_ids.end(), id) != res.evicted_ids.end();
    };
    clips_.erase(std::remove_if(clips_.begin(), clips_.end(),
                                [&](const ProtectedClip& c) { return evicted(c.id); }),
                 clips_.end());
    normal_.erase(std::remove_if(normal_.begin(), normal_.end(),
                                 [&](const RecordingSegmentRow& r) { return evicted(r.id); }),
                  normal_.end());
    return res;
}

}  // namespace core
}  // namespace railhmi
