#include "railhmi/core/clip_request.h"

namespace railhmi {
namespace core {

ClipRequest make_clip_request(const std::string& request_id, long long event_id,
                              const std::string& camera_id, long long trigger_wall_ms,
                              int pre_seconds, int post_seconds) {
    ClipRequest r;
    r.request_id = request_id;
    r.event_id = event_id;
    r.camera_id = camera_id;
    r.trigger_wall_ms = trigger_wall_ms;
    r.t0_wall_ms = trigger_wall_ms - static_cast<long long>(pre_seconds) * 1000;
    r.t1_wall_ms = trigger_wall_ms + static_cast<long long>(post_seconds) * 1000;
    return r;
}

bool clip_request_same_params(const ClipRequest& a, const ClipRequest& b) {
    return a.event_id == b.event_id && a.camera_id == b.camera_id &&
           a.trigger_wall_ms == b.trigger_wall_ms && a.t0_wall_ms == b.t0_wall_ms &&
           a.t1_wall_ms == b.t1_wall_ms;
}

const char* to_string(ClipRequestOutcome o) {
    switch (o) {
        case ClipRequestOutcome::Added: return "added";
        case ClipRequestOutcome::Duplicate: return "duplicate";
        case ClipRequestOutcome::Conflict: return "conflict";
    }
    return "?";
}

ClipRequestOutcome ClipRequestRegistry::submit(const ClipRequest& r) {
    auto it = jobs_.find(r.request_id);
    if (it == jobs_.end()) {
        jobs_.emplace(r.request_id, r);
        return ClipRequestOutcome::Added;
    }
    return clip_request_same_params(it->second, r) ? ClipRequestOutcome::Duplicate
                                                   : ClipRequestOutcome::Conflict;
}

bool ClipRequestRegistry::known(const std::string& request_id) const {
    return jobs_.find(request_id) != jobs_.end();
}

const ClipRequest* ClipRequestRegistry::find(const std::string& request_id) const {
    auto it = jobs_.find(request_id);
    return it == jobs_.end() ? nullptr : &it->second;
}

const char* to_string(ClipAvailability a) {
    switch (a) {
        case ClipAvailability::Complete: return "complete";
        case ClipAvailability::Incomplete: return "incomplete";
        case ClipAvailability::Unavailable: return "unavailable";
    }
    return "?";
}

bool parse_clip_availability(const std::string& s, ClipAvailability* out) {
    if (s == "complete") { *out = ClipAvailability::Complete; return true; }
    if (s == "incomplete") { *out = ClipAvailability::Incomplete; return true; }
    if (s == "unavailable") { *out = ClipAvailability::Unavailable; return true; }
    return false;
}

}  // namespace core
}  // namespace railhmi
