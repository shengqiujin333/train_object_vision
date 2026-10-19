// railhmi-vision-tracking (视觉推理 ITEM-008)
//
// 轻量目标跟踪与多帧确认协同探针：
//   * 无专用跟踪模型时用标签 + IoU（可选中心距回退）贪心关联，生成稳定 track_id（>=1，永不 0）；
//   * 同一目标连续命中只更新同一事件；不同目标互不清除确认；
//   * 丢失超过 max_misses 删除；generation 变化 reset()，旧 id 不复用；
//   * 跟踪不可用时显式 null（-1）或相机级适配（共享 -1，**不是 0**）。
//
// 用法:
//   railhmi-vision-tracking --selftest [--out log] [--json file]
//   railhmi-vision-tracking --fixture same_target|two_targets|label_guard|miss_drop|
//       generation_reset|camera_level|envelope_only|max_tracks
//   railhmi-vision-tracking --config DIR [--policy lightweight|camera|envelope]
//
// 退出码: 0 通过；1 失败；2 参数错误。

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/rules.h"
#include "railhmi/core/vision_semantic_adapter.h"
#include "railhmi/core/vision_tracker.h"

#ifndef RAILHMI_CONFIG_DIR
#define RAILHMI_CONFIG_DIR "config"
#endif

using namespace railhmi::core;

namespace {

int g_fail = 0;
std::vector<std::string> g_lines;
std::ofstream g_fo;

void emit(const std::string& l) {
    std::printf("%s\n", l.c_str());
    g_lines.push_back(l);
    if (g_fo) g_fo << l << "\n";
}
void check(bool ok, const std::string& name, const std::string& detail = "") {
    emit(std::string("[") + (ok ? "PASS" : "FAIL") + "] " + name + (detail.empty() ? "" : (" :: " + detail)));
    if (!ok) ++g_fail;
}

EnvelopeBox eb(const std::string& label, double x, double y, double w = 0.1, double h = 0.1,
               double conf = 0.9) {
    EnvelopeBox b;
    b.label = label;
    b.x = static_cast<float>(x);
    b.y = static_cast<float>(y);
    b.w = static_cast<float>(w);
    b.h = static_cast<float>(h);
    b.confidence = static_cast<float>(conf);
    b.in_roi = true;  // 夹具：默认在轨道内（ITEM-005 语义）
    return b;
}

FrameId fid(std::uint64_t gen, std::uint64_t seq) {
    FrameId f;
    f.session_id = "s";
    f.camera_id = "CAM-01";
    f.generation = gen;
    f.seq = seq;
    return f;
}

InferenceEnvelope mk(const FrameId& f, const std::vector<EnvelopeBox>& boxes) {
    EnvelopeBuildInput in;
    in.frame = f;
    in.profile = AiProfile::Front;
    in.model_version = "m";
    in.capture_mono_us = 1000;
    in.result_mono_us = 20000;
    in.requires_segmentation = false;
    in.boxes = nullptr;  // 直接用自己构造的 EnvelopeBox（见下）
    InferenceEnvelope e = build_envelope(in);
    e.boxes = boxes;
    return e;
}

SemanticConfig scfg(TrackPolicy p, double conf = 0.5) {
    SemanticConfig c;
    c.profile = AiProfile::Front;
    c.allowed_labels = {"person", "obstacle"};
    c.known_labels = {"person", "obstacle"};
    c.confidence_min = conf;
    c.track_policy = p;
    c.tracker.iou_match_min = 0.3f;
    c.tracker.max_misses = 3;
    c.tracker.max_tracks = 32;
    c.tracker.max_gap_ms = 1000;
    return c;
}

RuleConfig rcfg() {
    RuleConfig r;
    r.confirm_frames = 3;
    r.confirm_window_ms = 500;
    r.confidence_min = 0.5;
    r.suppress_window_ms = 3000;
    return r;
}

int run_selftest() {
    // ---------- 跟踪器本体 ----------
    {
        LightweightTracker t;
        auto a = t.update({eb("person", 0.4, 0.7)}, 100);
        check(a.size() == 1 && a[0].track_id == 1 && a[0].is_new, "new box -> id 1");
        auto b = t.update({eb("person", 0.4, 0.7)}, 200);
        check(b.size() == 1 && b[0].track_id == 1 && !b[0].is_new && t.matched() >= 1,
              "same box -> same id");
        auto c = t.update({eb("person", 0.41, 0.71)}, 300);
        check(c.size() == 1 && c[0].track_id == 1, "small move -> same id (IoU)");
        auto d = t.update({eb("person", 0.85, 0.1)}, 400);
        check(d.size() == 1 && d[0].track_id == 2, "far box -> new id");
        check(a[0].track_id != 0 && b[0].track_id != 0 && c[0].track_id != 0, "ids never 0");
    }
    {
        // 不同标签同位置不得串 id
        LightweightTracker t;
        (void)t.update({eb("person", 0.4, 0.7)}, 100);
        auto r = t.update({eb("obstacle", 0.4, 0.7)}, 200);
        check(r.size() == 1 && r[0].track_id == 2, "different label not matched");
    }
    {
        // 中心距回退（IoU=0、中心很近）
        TrackerConfig cfg;
        cfg.iou_match_min = 0.9f;
        cfg.center_fallback = true;
        cfg.center_dist_max = 0.3f;
        LightweightTracker t(cfg);
        (void)t.update({eb("person", 0.4, 0.7, 0.1, 0.1)}, 100);
        auto r = t.update({eb("person", 0.45, 0.72, 0.1, 0.1)}, 200);
        check(r.size() == 1 && r[0].track_id == 1, "center fallback matches");
    }
    {
        // 丢失超 max_misses -> 删除；重现 -> 新 id
        TrackerConfig cfg;
        cfg.max_misses = 2;
        LightweightTracker t(cfg);
        (void)t.update({eb("person", 0.4, 0.7)}, 100);
        for (int i = 0; i < 3; ++i) (void)t.update({}, 200 + i * 100);
        check(t.size() == 0, "missed track dropped", std::to_string(t.size()));
        auto r = t.update({eb("person", 0.4, 0.7)}, 700);
        check(r.size() == 1 && r[0].track_id == 2 && r[0].is_new, "reappear -> new id (not reused)");
    }
    {
        // reset：清空 track，但 id 继续单调
        LightweightTracker t;
        (void)t.update({eb("person", 0.4, 0.7)}, 100);
        t.reset();
        check(t.size() == 0, "reset clears tracks");
        auto r = t.update({eb("person", 0.4, 0.7)}, 200);
        check(r.size() == 1 && r[0].track_id == 2 && r[0].track_id != 0,
              "post-reset id continues (no reuse)");
    }
    {
        // max_tracks 上限：超出的框显式 -1
        TrackerConfig cfg;
        cfg.max_tracks = 1;
        LightweightTracker t(cfg);
        auto r = t.update({eb("person", 0.1, 0.1), eb("person", 0.8, 0.8)}, 100);
        check(r.size() == 2 && r[0].track_id == 1 && r[1].track_id == -1 && t.untracked() == 1,
              "max_tracks -> untracked (-1), not fabricated");
    }
    {
        // 时间鸿沟 -> 过期不匹配
        TrackerConfig cfg;
        cfg.max_gap_ms = 100;
        LightweightTracker t(cfg);
        (void)t.update({eb("person", 0.4, 0.7)}, 100);
        auto r = t.update({eb("person", 0.4, 0.7)}, 1000);
        check(r.size() == 1 && r[0].track_id == 2, "stale gap -> new id");
    }

    // ---------- 适配器集成 ----------
    {
        // Lightweight：同一目标 3 帧 -> 同一 track、1 个事件
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg(TrackPolicy::Lightweight));
        std::size_t events = 0;
        int32_t id = -1;
        for (int i = 0; i < 3; ++i) {
            SemanticResult r;
            auto ev = ad.adapt_and_dispatch(mk(fid(1, i + 1), {eb("person", 0.4 + 0.005 * i, 0.7)}),
                                            (i + 1) * 100, &rules, &r);
            events += ev.size();
            if (!r.detections.empty()) id = r.detections[0].track_id;
        }
        check(events == 1 && rules.events().size() == 1 && id == 1,
              "lightweight: same target -> same id + 1 event", "id=" + std::to_string(id));
        check(!rules.events().empty() && rules.events()[0].track_id == 1, "event carries track id");
    }
    {
        // Lightweight：两个目标 -> 两个 id、两个事件
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg(TrackPolicy::Lightweight));
        for (int i = 0; i < 3; ++i)
            (void)ad.adapt_and_dispatch(
                mk(fid(1, i + 1), {eb("person", 0.1 + 0.005 * i, 0.7), eb("person", 0.7 + 0.005 * i, 0.7)}),
                (i + 1) * 100, &rules, nullptr);
        check(rules.events().size() == 2, "lightweight: two targets -> 2 ids/events",
              std::to_string(rules.events().size()));
    }
    {
        // generation 变化：tracker reset + 规则清理；新代新 id、重新累计 3 帧
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg(TrackPolicy::Lightweight));
        for (int i = 0; i < 2; ++i)
            (void)ad.adapt_and_dispatch(mk(fid(1, i + 1), {eb("person", 0.4, 0.7)}), (i + 1) * 100,
                                        &rules, nullptr);
        SemanticResult r3;
        auto ev3 = ad.adapt_and_dispatch(mk(fid(2, 1), {eb("person", 0.4, 0.7)}), 300, &rules, &r3);
        check(ev3.empty(), "generation change: no alarm on first new frame");
        check(r3.stats.tracker_resets == 1 && r3.stats.generation_reset_keys > 0,
              "generation change resets tracker + rule keys",
              std::to_string(r3.stats.tracker_resets) + "/" +
                  std::to_string(r3.stats.generation_reset_keys));
        const int32_t new_id = r3.detections.empty() ? -1 : r3.detections[0].track_id;
        check(new_id != 1 && new_id >= 1, "new generation gets new track id (no reuse)",
              std::to_string(new_id));
        auto ev4 = ad.adapt_and_dispatch(mk(fid(2, 2), {eb("person", 0.4, 0.7)}), 400, &rules, nullptr);
        auto ev5 = ad.adapt_and_dispatch(mk(fid(2, 3), {eb("person", 0.4, 0.7)}), 500, &rules, nullptr);
        check(ev4.empty() && ev5.size() == 1, "new generation re-confirms (3 frames)");
    }
    {
        // CameraLevel：共享 -1（不是 0），3 帧 -> 1 个相机级事件
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg(TrackPolicy::CameraLevel));
        std::size_t events = 0;
        int32_t id = 0;
        for (int i = 0; i < 3; ++i) {
            SemanticResult r;
            auto ev = ad.adapt_and_dispatch(
                mk(fid(1, i + 1), {eb("person", 0.1, 0.7), eb("person", 0.7, 0.7)}), (i + 1) * 100,
                &rules, &r);
            events += ev.size();
            if (!r.detections.empty()) id = r.detections[0].track_id;
        }
        check(events == 1 && id == -1, "camera-level: one camera event, track_id=-1 (not 0)",
              "events=" + std::to_string(events) + " id=" + std::to_string(id));
        check(!rules.events().empty() && rules.events()[0].track_id == -1, "event carries null track");
    }
    {
        // EnvelopeOnly：null track 丢弃（回归 ITEM-007）
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg(TrackPolicy::EnvelopeOnly));
        SemanticResult r;
        (void)ad.adapt_and_dispatch(mk(fid(1, 1), {eb("person", 0.4, 0.7)}), 100, &rules, &r);
        check(r.detections.empty() && r.stats.dropped_no_track == 1,
              "envelope_only: null track dropped (regression)");
    }
    emit("RESULT: " + std::string(g_fail == 0 ? "PASS" : "FAIL") + " failures=" + std::to_string(g_fail));
    return g_fail == 0 ? 0 : 1;
}

TrackPolicy parse_policy(const std::string& s) {
    if (s == "lightweight") return TrackPolicy::Lightweight;
    if (s == "camera" || s == "camera_level") return TrackPolicy::CameraLevel;
    return TrackPolicy::EnvelopeOnly;
}

int run_fixture(const std::string& name, TrackPolicy policy) {
    // 夹具实际生效的策略（camera_level/envelope_only 自带固定策略）。
    TrackPolicy eff = policy;
    if (name == "camera_level") eff = TrackPolicy::CameraLevel;
    else if (name == "envelope_only") eff = TrackPolicy::EnvelopeOnly;
    emit("type=config fixture=" + name + " policy=" + std::string(to_string(eff)));
    RuleConfig rc = rcfg();
    SemanticConfig sc = scfg(policy);

    if (name == "same_target") {
        RuleEngine rules(rc);
        SemanticAdapter ad(sc);
        std::size_t events = 0;
        for (int i = 0; i < 4; ++i) {
            SemanticResult r;
            auto ev = ad.adapt_and_dispatch(mk(fid(1, i + 1), {eb("person", 0.4 + 0.005 * i, 0.7)}),
                                            (i + 1) * 100, &rules, &r);
            events += ev.size();
            if (!r.detections.empty())
                emit("frame=" + std::to_string(i + 1) +
                     " track=" + std::to_string(r.detections[0].track_id) +
                     " events_total=" + std::to_string(rules.events().size()));
        }
        const bool pass = events == 1 && rules.events().size() == 1;
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    }
    if (name == "two_targets") {
        RuleEngine rules(rc);
        SemanticAdapter ad(sc);
        for (int i = 0; i < 3; ++i)
            (void)ad.adapt_and_dispatch(
                mk(fid(1, i + 1), {eb("person", 0.1 + 0.005 * i, 0.7), eb("person", 0.7 + 0.005 * i, 0.7)}),
                (i + 1) * 100, &rules, nullptr);
        emit("events=" + std::to_string(rules.events().size()));
        const bool pass = rules.events().size() == 2 &&
                          rules.events()[0].track_id != rules.events()[1].track_id;
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    }
    if (name == "label_guard") {
        LightweightTracker t(sc.tracker);
        (void)t.update({eb("person", 0.4, 0.7)}, 100);
        auto r = t.update({eb("obstacle", 0.4, 0.7)}, 200);
        emit("ids=" + std::to_string(r[0].track_id));
        const bool pass = r.size() == 1 && r[0].track_id == 2;
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    }
    if (name == "miss_drop") {
        TrackerConfig cfg = sc.tracker;
        cfg.max_misses = 2;
        LightweightTracker t(cfg);
        (void)t.update({eb("person", 0.4, 0.7)}, 100);
        for (int i = 0; i < 3; ++i) (void)t.update({}, 200 + i * 100);
        const bool dropped = t.size() == 0;
        auto r = t.update({eb("person", 0.4, 0.7)}, 700);
        emit("dropped=" + std::string(dropped ? "1" : "0") + " new_id=" +
             std::to_string(r.empty() ? -1 : r[0].track_id));
        const bool pass = dropped && r.size() == 1 && r[0].track_id == 2;
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    }
    if (name == "generation_reset") {
        RuleEngine rules(rc);
        SemanticAdapter ad(scfg(TrackPolicy::Lightweight));
        for (int i = 0; i < 2; ++i)
            (void)ad.adapt_and_dispatch(mk(fid(1, i + 1), {eb("person", 0.4, 0.7)}), (i + 1) * 100,
                                        &rules, nullptr);
        SemanticResult r3;
        auto ev3 = ad.adapt_and_dispatch(mk(fid(2, 1), {eb("person", 0.4, 0.7)}), 300, &rules, &r3);
        auto ev4 = ad.adapt_and_dispatch(mk(fid(2, 2), {eb("person", 0.4, 0.7)}), 400, &rules, nullptr);
        auto ev5 = ad.adapt_and_dispatch(mk(fid(2, 3), {eb("person", 0.4, 0.7)}), 500, &rules, nullptr);
        const int32_t id = r3.detections.empty() ? -1 : r3.detections[0].track_id;
        emit("tracker_resets=" + std::to_string(r3.stats.tracker_resets) +
             " gen_keys=" + std::to_string(r3.stats.generation_reset_keys) +
             " new_id=" + std::to_string(id) + " events=" + std::to_string(ev3.size()) + "/" +
             std::to_string(ev4.size()) + "/" + std::to_string(ev5.size()));
        const bool pass = ev3.empty() && ev4.empty() && ev5.size() == 1 &&
                          r3.stats.tracker_resets == 1 && id >= 1 && id != 1;
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    }
    if (name == "camera_level") {
        RuleEngine rules(rc);
        SemanticAdapter ad(scfg(TrackPolicy::CameraLevel));
        int32_t id = 0;
        for (int i = 0; i < 3; ++i) {
            SemanticResult r;
            (void)ad.adapt_and_dispatch(
                mk(fid(1, i + 1), {eb("person", 0.1, 0.7), eb("person", 0.7, 0.7)}), (i + 1) * 100,
                &rules, &r);
            if (!r.detections.empty()) id = r.detections[0].track_id;
        }
        emit("det_id=" + std::to_string(id) + " events=" + std::to_string(rules.events().size()));
        const bool pass = id == -1 && rules.events().size() == 1 && rules.events()[0].track_id == -1;
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    }
    if (name == "envelope_only") {
        RuleEngine rules(rc);
        SemanticAdapter ad(scfg(TrackPolicy::EnvelopeOnly));
        SemanticResult r;
        (void)ad.adapt_and_dispatch(mk(fid(1, 1), {eb("person", 0.4, 0.7)}), 100, &rules, &r);
        emit("det=" + std::to_string(r.detections.size()) + " no_track=" +
             std::to_string(r.stats.dropped_no_track));
        const bool pass = r.detections.empty() && r.stats.dropped_no_track == 1;
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    }
    if (name == "max_tracks") {
        TrackerConfig cfg = sc.tracker;
        cfg.max_tracks = 1;
        LightweightTracker t(cfg);
        auto r = t.update({eb("person", 0.1, 0.1), eb("person", 0.8, 0.8)}, 100);
        emit("ids=" + std::to_string(r[0].track_id) + "," + std::to_string(r[1].track_id) +
             " untracked=" + std::to_string(t.untracked()));
        const bool pass = r.size() == 2 && r[0].track_id == 1 && r[1].track_id == -1 &&
                          t.untracked() == 1;
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    }
    std::fprintf(stderr, "unknown fixture: %s\n", name.c_str());
    return 2;
}

int run_config(const std::string& dir, TrackPolicy policy) {
    ConfigLoadResult cfg = load_config_dir(dir);
    if (!cfg.ok) {
        emit("type=error config invalid");
        for (const auto& e : cfg.errors) emit("  " + e.str());
        return 1;
    }
    RuleConfig rc;
    rc.confirm_frames = cfg.settings.thresholds.confirm_frames;
    rc.confirm_window_ms = cfg.settings.thresholds.confirm_window_ms;
    rc.confidence_min = cfg.settings.thresholds.confidence_min;
    rc.suppress_window_ms = cfg.settings.thresholds.suppress_window_ms;
    emit("type=config dir=" + dir + " confirm_frames=" + std::to_string(rc.confirm_frames) +
         " confidence_min=" + std::to_string(rc.confidence_min) +
         " suppress_window_ms=" + std::to_string(rc.suppress_window_ms));
    // 使用配置阈值 + 指定策略跑 same_target
    RuleEngine rules(rc);
    SemanticAdapter ad(scfg(policy, rc.confidence_min));
    std::size_t events = 0;
    for (int i = 0; i < rc.confirm_frames; ++i) {
        auto ev = ad.adapt_and_dispatch(mk(fid(1, i + 1), {eb("person", 0.4 + 0.005 * i, 0.7)}),
                                        (i + 1) * 100, &rules, nullptr);
        events += ev.size();
    }
    emit("events=" + std::to_string(events));
    const bool pass = events == 1;
    emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
    return pass ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string mode, name = "same_target", out, json, dir = RAILHMI_CONFIG_DIR, policy_s = "lightweight";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--selftest") mode = "selftest";
        else if (a == "--fixture") { next(&name); mode = "fixture"; }
        else if (a == "--config") { next(&dir); mode = "config"; }
        else if (a == "--policy") next(&policy_s);
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-vision-tracking --selftest | --fixture NAME | --config DIR "
                        "[--policy lightweight|camera|envelope]\n");
            return 0;
        } else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }
    if (mode.empty()) { std::fprintf(stderr, "need --selftest/--fixture/--config\n"); return 2; }
    if (!out.empty()) g_fo.open(out, std::ios::trunc);
    int rc = 0;
    if (mode == "selftest") rc = run_selftest();
    else if (mode == "fixture") rc = run_fixture(name, parse_policy(policy_s));
    else rc = run_config(dir, parse_policy(policy_s));
    if (!json.empty()) {
        std::ofstream fj(json, std::ios::trunc);
        if (fj) {
            fj << "{\"mode\":\"" << mode << "\",\"failures\":" << g_fail << ",\"lines\":[";
            for (std::size_t i = 0; i < g_lines.size(); ++i) {
                if (i) fj << ",";
                std::string esc;
                for (char ch : g_lines[i]) { if (ch == '"' || ch == '\\') esc += '\\'; esc += ch; }
                fj << "\"" << esc << "\"";
            }
            fj << "]}";
        }
    }
    return rc;
}
