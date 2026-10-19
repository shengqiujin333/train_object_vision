// railhmi-core: 同帧两阶段判定实现（视觉推理 ITEM-005）。

#include "railhmi/core/vision_two_stage.h"

#include <algorithm>
#include <cmath>

namespace railhmi {
namespace core {

namespace {

bool fail(std::string* err, const char* reason, const std::string& detail = std::string()) {
    if (err) *err = detail.empty() ? std::string(reason) : (std::string(reason) + ": " + detail);
    return false;
}

bool mask_positive_at(const std::vector<SegMask>& masks, int image_w, int image_h, double cx,
                      double cy) {
    int px = static_cast<int>(std::floor(cx * image_w));
    int py = static_cast<int>(std::floor(cy * image_h));
    px = std::max(0, std::min(px, image_w - 1));
    py = std::max(0, std::min(py, image_h - 1));
    for (const SegMask& m : masks) {
        if (m.width != image_w || m.height != image_h) continue;
        if (m.stride_bytes < m.width) continue;
        const std::size_t idx = static_cast<std::size_t>(py) * m.stride_bytes + px;
        if (idx < m.plane.size() && m.plane[idx] != 0) return true;
    }
    return false;
}

bool masks_usable(const std::vector<SegMask>& masks, int image_w, int image_h, std::string* why) {
    if (masks.empty()) {
        *why = "rail mask empty";
        return false;
    }
    bool aligned = false;
    bool positive = false;
    for (const SegMask& m : masks) {
        if (m.width == image_w && m.height == image_h && m.stride_bytes >= m.width &&
            m.plane.size() >= static_cast<std::size_t>(m.stride_bytes) * m.height) {
            aligned = true;
        }
        if (m.positive > 0) positive = true;
    }
    if (!aligned) {
        *why = "rail mask not aligned to image size";
        return false;
    }
    if (!positive) {
        *why = "rail mask has no positive pixel";
        return false;
    }
    return true;
}

}  // namespace

bool same_frame(const FrameId& a, const FrameId& b) { return a == b; }

bool evaluate_two_stage(const TwoStageInput& in, const TwoStageConfig& cfg, TwoStageResult* out,
                        std::string* err) {
    if (err) err->clear();
    if (!out) return fail(err, two_stage_reason::kBadConfig(), "null output");
    *out = TwoStageResult{};
    out->frame = in.frame;
    if (in.image_w <= 0 || in.image_h <= 0) {
        out->reason = two_stage_reason::kBadConfig();
        return fail(err, two_stage_reason::kBadConfig(), "non-positive image size");
    }

    // 1) 同帧身份：检测/分割必须与当前帧一致（禁止跨帧充当实时）。
    if (cfg.require_same_frame) {
        if (!same_frame(in.detect_frame, in.frame)) {
            out->reason = two_stage_reason::kFrameMismatch();
            out->detail = "detect_frame != frame";
            return true;  // 有效结果为“不可用”
        }
        if (in.seg_available && !same_frame(in.seg_frame, in.frame)) {
            out->reason = two_stage_reason::kFrameMismatch();
            out->detail = "seg_frame != frame";
            return true;
        }
    }

    // 2) 可用性：任一侧不可用/掩码为空 -> 判定不可用，绝不输出“轨道内安全”。
    if (!in.detect_available) {
        out->reason = two_stage_reason::kUnavailable();
        out->detail = "detection unavailable";
        return true;
    }
    if (!in.seg_available || !in.rail_masks) {
        out->reason = two_stage_reason::kUnavailable();
        out->detail = "segmentation unavailable";
        return true;
    }
    std::string why;
    if (!masks_usable(*in.rail_masks, in.image_w, in.image_h, &why)) {
        out->reason = two_stage_reason::kUnavailable();
        out->detail = why;
        return true;
    }

    out->available = true;
    out->person_total = in.persons ? static_cast<long long>(in.persons->size()) : 0;
    if (in.persons) {
        for (const DetectBox& p : *in.persons) {
            const double cx = p.x + p.w / 2.0;
            const double cy = p.y + p.h / 2.0;
            if (cx < 0.0 || cx > 1.0 || cy < 0.0 || cy > 1.0) {
                ++out->dropped_out_of_bounds;
                continue;
            }
            TwoStageDetection d;
            d.box = p;
            d.label = "person";
            d.mask_hit = mask_positive_at(*in.rail_masks, in.image_w, in.image_h, cx, cy);
            if (!d.mask_hit) {
                ++out->dropped_no_mask;
                out->detections.push_back(d);
                continue;
            }
            ++out->mask_hit;
            if (cfg.danger_zone) {
                d.in_danger_zone = roi_contains(*cfg.danger_zone, cx, cy);
                if (!d.in_danger_zone) ++out->dropped_out_of_zone;
            }
            d.on_rail_alarm = d.mask_hit && (cfg.danger_zone ? d.in_danger_zone : true);
            if (d.on_rail_alarm) ++out->alarm_hits;
            out->detections.push_back(d);
        }
    }

    out->ok = true;
    out->reason = out->alarm_hits > 0 ? two_stage_reason::kOk()
                                      : (out->person_total == 0 ? two_stage_reason::kNoPerson()
                                                                : two_stage_reason::kNoAlarm());
    out->detail = "persons=" + std::to_string(out->person_total) +
                  " mask_hit=" + std::to_string(out->mask_hit) +
                  " alarms=" + std::to_string(out->alarm_hits) +
                  (cfg.danger_zone ? " danger_zone=1" : " danger_zone=0");
    return true;
}

bool TwoStageCache::lookup(const FrameId& frame, long long now_mono_us, std::vector<SegMask>* masks,
                           long long* age_ms) const {
    if (age_ms) *age_ms = -1;
    if (!cfg_.enabled || !has_ || cfg_.max_age_ms <= 0) return false;
    if (masks_.empty()) return false;
    if (cfg_.reject_on_generation_change) {
        if (frame.session_id != frame_.session_id || frame.camera_id != frame_.camera_id ||
            frame.generation != frame_.generation) {
            return false;
        }
    }
    const long long age = (now_mono_us - mono_us_) / 1000;
    if (age < 0 || age > cfg_.max_age_ms) return false;
    if (age_ms) *age_ms = age;
    if (masks) *masks = masks_;
    return true;
}

void TwoStageCache::store(const FrameId& frame, long long now_mono_us,
                          const std::vector<SegMask>& masks) {
    if (!cfg_.enabled || cfg_.max_age_ms <= 0 || masks.empty()) return;
    frame_ = frame;
    mono_us_ = now_mono_us;
    masks_ = masks;
    has_ = true;
}

void TwoStageCache::clear() {
    has_ = false;
    masks_.clear();
    frame_ = FrameId{};
    mono_us_ = 0;
}

TwoStageEvaluator::TwoStageEvaluator(TwoStageConfig cfg, std::string session_id)
    : cfg_(std::move(cfg)), gate_(std::move(session_id)), cache_(cfg_.cache) {}

TwoStageResult TwoStageEvaluator::evaluate(const TwoStageInput& in) {
    TwoStageResult r;
    std::string err;
    const FrameAdmission adm = gate_.admit(in.frame);
    if (adm != FrameAdmission::Accepted) {
        r.frame = in.frame;
        r.ok = false;
        r.available = false;
        r.reason = two_stage_reason::kStale();
        r.detail = std::string("frame admission=") + to_string(adm);
        return r;
    }

    // 本帧分割可用：正常判定，并按配置更新缓存。
    if (in.seg_available && in.rail_masks && !in.rail_masks->empty() &&
        same_frame(in.seg_frame, in.frame)) {
        evaluate_two_stage(in, cfg_, &r, &err);
        if (cfg_.cache.enabled && r.available) cache_.store(in.frame, in.now_mono_us, *in.rail_masks);
        return r;
    }

    // 本帧分割不可用：仅当缓存已登记且策略允许时复用同 generation 的近期掩码。
    std::vector<SegMask> cached;
    long long age = -1;
    if (cache_.lookup(in.frame, in.now_mono_us, &cached, &age)) {
        TwoStageInput eff = in;
        eff.seg_available = true;
        eff.seg_frame = in.frame;
        eff.rail_masks = &cached;
        evaluate_two_stage(eff, cfg_, &r, &err);
        r.used_cache = true;
        r.cache_age_ms = age;
        return r;
    }

    evaluate_two_stage(in, cfg_, &r, &err);
    return r;
}

}  // namespace core
}  // namespace railhmi
