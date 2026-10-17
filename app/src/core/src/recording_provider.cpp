#include "railhmi/core/recording_provider.h"

#include <algorithm>

namespace railhmi {
namespace core {

const char* to_string(ProviderStatus s) {
    switch (s) {
        case ProviderStatus::Ok: return "ok";
        case ProviderStatus::Partial: return "partial";
        case ProviderStatus::Unavailable: return "unavailable";
        case ProviderStatus::Error: return "error";
    }
    return "?";
}

bool parse_provider_status(const std::string& s, ProviderStatus* out) {
    if (s == "ok") { *out = ProviderStatus::Ok; return true; }
    if (s == "partial") { *out = ProviderStatus::Partial; return true; }
    if (s == "unavailable") { *out = ProviderStatus::Unavailable; return true; }
    if (s == "error") { *out = ProviderStatus::Error; return true; }
    return false;
}

bool provider_deadline_expired(const ProviderRequest& r, long long now_mono_us) {
    return r.deadline_mono_us > 0 && now_mono_us > r.deadline_mono_us;
}

long long covered_ms_in_window(long long t0_ms, long long t1_ms, const std::vector<SegmentRef>& segments) {
    if (t1_ms <= t0_ms) return 0;
    std::vector<std::pair<long long, long long>> iv;
    for (const auto& s : segments) {
        if (!s.usable) continue;
        const long long a = std::max(t0_ms, s.t0_ms);
        const long long b = std::min(t1_ms, s.t1_ms);
        if (b > a) iv.emplace_back(a, b);
    }
    if (iv.empty()) return 0;
    std::sort(iv.begin(), iv.end());
    long long covered = 0;
    long long cur_a = iv[0].first, cur_b = iv[0].second;
    for (std::size_t i = 1; i < iv.size(); ++i) {
        if (iv[i].first <= cur_b) {
            cur_b = std::max(cur_b, iv[i].second);
        } else {
            covered += cur_b - cur_a;
            cur_a = iv[i].first;
            cur_b = iv[i].second;
        }
    }
    covered += cur_b - cur_a;
    return covered;
}

SegmentQueryResult classify_segments(const ProviderRequest& req,
                                     const std::vector<SegmentRef>& candidates,
                                     const std::vector<RecordingGapRow>& all_gaps,
                                     bool source_configured) {
    SegmentQueryResult r;
    r.request_id = req.request_id;
    r.camera_id = req.camera_id;
    r.window_t0_ms = req.t0_ms;
    r.window_t1_ms = req.t1_ms;
    r.source_configured = source_configured;

    if (!source_configured) {
        r.status = ProviderStatus::Unavailable;
        r.reason = provider_reason::kUnconfigured;
        return r;
    }

    for (const auto& s : candidates) {
        if (s.camera_id != req.camera_id) continue;
        if (s.t1_ms <= req.t0_ms || s.t0_ms >= req.t1_ms) continue;  // 与窗口不相交
        r.segments.push_back(s);
    }
    for (const auto& g : all_gaps) {
        if (g.cam != req.camera_id) continue;
        if (g.t1_ms <= req.t0_ms || g.t0_ms >= req.t1_ms) continue;
        r.gaps.push_back(g);
    }

    if (r.segments.empty()) {
        // 查询成功但无覆盖：空列表，不声称可播放。
        r.status = ProviderStatus::Ok;
        r.coverage_complete = false;
        return r;
    }

    r.covered_ms = covered_ms_in_window(req.t0_ms, req.t1_ms, r.segments);
    const bool any_usable = std::any_of(r.segments.begin(), r.segments.end(),
                                        [](const SegmentRef& s) { return s.usable; });
    if (!any_usable) {
        r.status = ProviderStatus::Unavailable;
        r.reason = provider_reason::kNotFound;
        return r;
    }
    const long long window = req.t1_ms - req.t0_ms;
    if (r.covered_ms >= window) {
        r.status = ProviderStatus::Ok;
        r.coverage_complete = true;
    } else {
        r.status = ProviderStatus::Partial;
    }
    return r;
}

}  // namespace core
}  // namespace railhmi
