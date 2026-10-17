#include "railhmi/core/frame_identity.h"

#include <atomic>
#include <chrono>
#include <sstream>

namespace railhmi {
namespace core {

namespace {

std::uint64_t steady_now_us() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

}  // namespace

std::string make_session_id() {
    static std::atomic<std::uint64_t> counter{0};
    using namespace std::chrono;
    const auto wall_ms =
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    std::ostringstream o;
    o << std::hex << "s" << wall_ms << "-" << (steady_now_us() & 0xffffffu) << "-"
      << counter.fetch_add(1);
    return o.str();
}

FrameSequencer::FrameSequencer(std::string session_id, std::string camera_id)
    : session_id_(std::move(session_id)), camera_id_(std::move(camera_id)) {}

std::uint64_t FrameSequencer::begin_generation() {
    ++generation_;
    seq_ = 0;
    return generation_;
}

FrameMeta FrameSequencer::next(std::int64_t capture_mono_us, std::int64_t capture_wall_ms,
                               std::int64_t pts_ns) {
    FrameMeta m;
    m.id.session_id = session_id_;
    m.id.camera_id = camera_id_;
    m.id.generation = generation_;
    m.id.seq = ++seq_;
    m.capture_mono_us = capture_mono_us;
    m.capture_wall_ms = capture_wall_ms;
    m.pts_ns = pts_ns;
    return m;
}

const char* to_string(FrameAdmission a) {
    switch (a) {
        case FrameAdmission::Accepted: return "accepted";
        case FrameAdmission::StaleSession: return "stale_session";
        case FrameAdmission::StaleGeneration: return "stale_generation";
        case FrameAdmission::DuplicateSeq: return "duplicate_seq";
        case FrameAdmission::OutOfOrderSeq: return "out_of_order_seq";
    }
    return "?";
}

FrameGate::FrameGate(std::string expected_session_id) : session_id_(std::move(expected_session_id)) {}

void FrameGate::reset(std::string session_id) {
    session_id_ = std::move(session_id);
    generation_ = 0;
    last_seq_ = 0;
    has_seq_ = false;
}

FrameAdmission FrameGate::admit(const FrameId& id) {
    if (session_id_.empty()) {
        session_id_ = id.session_id;
    } else if (id.session_id != session_id_) {
        return FrameAdmission::StaleSession;
    }

    if (id.generation < generation_) return FrameAdmission::StaleGeneration;
    if (id.generation > generation_) {
        generation_ = id.generation;
        last_seq_ = 0;
        has_seq_ = false;
    }

    if (has_seq_) {
        if (id.seq == last_seq_) return FrameAdmission::DuplicateSeq;
        if (id.seq < last_seq_) return FrameAdmission::OutOfOrderSeq;
    }
    last_seq_ = id.seq;
    has_seq_ = true;
    return FrameAdmission::Accepted;
}

bool FrameGate::is_current(const FrameId& id) const {
    if (!session_id_.empty() && id.session_id != session_id_) return false;
    return id.generation == generation_;
}

}  // namespace core
}  // namespace railhmi
