#include "railhmi/core/retention.h"

#include <algorithm>

namespace railhmi {
namespace core {

namespace {
const long long kGiB = 1024LL * 1024LL * 1024LL;
const long long kDayMs = 86400000LL;
}  // namespace

RetentionResult apply_retention(const std::vector<RecordingSegmentRow>& segments,
                                long long now_ms, const RetentionPolicy& policy) {
    RetentionResult r;
    const std::size_t n = segments.size();
    std::vector<char> evicted(n, 0);
    long long total = 0;
    for (const auto& s : segments) total += s.size_bytes;

    auto do_evict = [&](std::size_t i) {
        if (evicted[i]) return;
        evicted[i] = 1;
        r.evicted_ids.push_back(segments[i].id);
        r.evicted_bytes += segments[i].size_bytes;
        total -= segments[i].size_bytes;
        if (segments[i].kind == SegmentKind::Critical) ++r.evicted_critical;
        else ++r.evicted_continuous;
    };

    // 1) 期限淘汰：仅普通片段（超过 retention_days 未结束）
    const long long age_limit_ms = static_cast<long long>(policy.retention_days) * kDayMs;
    for (std::size_t i = 0; i < n; ++i) {
        const auto& s = segments[i];
        if (s.kind == SegmentKind::Continuous && (now_ms - s.t1_ms) > age_limit_ms) {
            do_evict(i);
        }
    }

    // 2) 容量淘汰：普通最旧优先，其次保护最旧
    std::vector<std::size_t> order;
    order.reserve(n);
    for (std::size_t i = 0; i < n; ++i) if (!evicted[i]) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const bool ca = segments[a].kind == SegmentKind::Continuous;
        const bool cb = segments[b].kind == SegmentKind::Continuous;
        if (ca != cb) return ca;  // 普通(true) 排前
        if (segments[a].t0_ms != segments[b].t0_ms) return segments[a].t0_ms < segments[b].t0_ms;
        return segments[a].id < segments[b].id;
    });

    const long long limit_bytes = static_cast<long long>(policy.max_gb * static_cast<double>(kGiB));
    for (std::size_t idx : order) {
        if (total <= limit_bytes) break;
        do_evict(idx);
    }

    r.retained_bytes = total;
    return r;
}

void add_gap(std::vector<RecordingGapRow>& gaps, const std::string& cam,
             long long t0_ms, long long t1_ms, const std::string& reason) {
    if (t1_ms < t0_ms) std::swap(t0_ms, t1_ms);
    long long lo = t0_ms;
    long long hi = t1_ms;

    std::vector<RecordingGapRow> same;
    std::vector<RecordingGapRow> others;
    for (const auto& g : gaps) {
        if (g.cam == cam) same.push_back(g);
        else others.push_back(g);
    }

    // 迭代吸收：合并区间会扩大 [lo,hi]，可能又涵盖之前不重叠的区间（桥接），
    // 直到一整轮无新增吸弶为止。闭合区间：重叠或相邻（相差≤1ms）即可合并。
    std::vector<char> absorbed(same.size(), 0);
    std::string merged_reason;
    bool have_reason = false;
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < same.size(); ++i) {
            if (absorbed[i]) continue;
            const RecordingGapRow& g = same[i];
            if (!((lo <= g.t1_ms + 1) && (g.t0_ms <= hi + 1))) continue;
            if (g.t0_ms < lo) lo = g.t0_ms;
            if (g.t1_ms > hi) hi = g.t1_ms;
            absorbed[i] = 1;
            changed = true;
            if (!have_reason && !g.reason.empty()) {
                merged_reason = g.reason;  // 先存在的非空 reason 优先
                have_reason = true;
            }
        }
    }
    if (!have_reason) merged_reason = reason;

    std::vector<RecordingGapRow> result;
    result.reserve(others.size() + same.size() + 1);
    for (const auto& g : others) result.push_back(g);
    for (std::size_t i = 0; i < same.size(); ++i) {
        if (!absorbed[i]) result.push_back(same[i]);
    }
    RecordingGapRow merged;
    merged.cam = cam;
    merged.t0_ms = lo;
    merged.t1_ms = hi;
    merged.reason = merged_reason;
    result.push_back(merged);

    std::sort(result.begin(), result.end(), [](const RecordingGapRow& a, const RecordingGapRow& b) {
        if (a.cam != b.cam) return a.cam < b.cam;
        if (a.t0_ms != b.t0_ms) return a.t0_ms < b.t0_ms;
        return a.id < b.id;
    });
    gaps.swap(result);
}

}  // namespace core
}  // namespace railhmi
