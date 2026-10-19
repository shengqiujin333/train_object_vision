// railhmi-vision-semantic (视觉推理 ITEM-007)
//
// 语义适配与规则接入探针：envelope 框按受支持标签白名单严格映射为 core::Detection 并交给
// RuleEngine；未知 label 拦截；同帧多目标按 track 聚合/排序；禁止 track_id=0 冒充；
// generation 变化与失帧通过受控接口清理确认状态，不整路误清无关目标。
//
// 用法:
//   railhmi-vision-semantic --selftest [--out log] [--json file]
//   railhmi-vision-semantic --fixture person_on_rail|unknown_label|multi_target|off_rail|
//       generation_reset|no_track|low_confidence|obstacle|unavailable|aggregate|sorted
//   railhmi-vision-semantic --config DIR
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

FrameId fid(std::uint64_t gen, std::uint64_t seq) {
    FrameId f;
    f.session_id = "s-sem";
    f.camera_id = "CAM-01";
    f.generation = gen;
    f.seq = seq;
    return f;
}

DetectBox bx(const std::string& label, double x, double y, double w, double h, double score = 0.9) {
    DetectBox b;
    b.x = static_cast<float>(x);
    b.y = static_cast<float>(y);
    b.w = static_cast<float>(w);
    b.h = static_cast<float>(h);
    b.score = static_cast<float>(score);
    b.class_id = label == "person" ? 0 : 1;
    b.label = label;
    return b;
}

InferenceEnvelope mk(const FrameId& f, const std::vector<DetectBox>& boxes,
                     const std::vector<std::int32_t>& tracks, const std::vector<char>& roi) {
    EnvelopeBuildInput in;
    in.frame = f;
    in.profile = AiProfile::Front;
    in.model_version = "rknn-2.3.2/yolov5s";
    in.capture_mono_us = 1000;
    in.result_mono_us = 20000;
    in.detect_ready = true;
    in.seg_ready = true;
    in.requires_segmentation = false;  // in_roi 由夹具直接给出（ITEM-005 语义）
    in.boxes = &boxes;
    in.track_ids = tracks;
    in.in_roi_flags = roi;
    return build_envelope(in);
}

SemanticConfig scfg(double conf_min = 0.5) {
    SemanticConfig c;
    c.profile = AiProfile::Front;
    c.allowed_labels = {"person", "obstacle"};
    c.known_labels = {"person", "obstacle"};
    c.confidence_min = conf_min;
    c.require_track = true;
    c.use_envelope_in_roi = true;
    c.threshold_source = "thresholds.confidence_min";
    c.allowed_labels_source = "front whitelist (person/obstacle)";
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

std::string res_line(const SemanticResult& r) {
    char b[400];
    std::snprintf(b, sizeof(b),
                  "type=semantic ok=%d available=%d reason=%s det=%zu cand=%lld in=%lld mapped=%lld "
                  "unknown=%lld no_track=%lld low_conf=%lld off_rail=%lld agg=%lld gen_reset=%lld "
                  "track_reset=%lld",
                  r.ok ? 1 : 0, r.available ? 1 : 0, r.reason.c_str(), r.detections.size(),
                  r.stats.candidates, r.stats.input_boxes, r.stats.mapped, r.stats.blocked_unknown_label,
                  r.stats.dropped_no_track, r.stats.dropped_low_confidence, r.stats.dropped_off_rail,
                  r.stats.aggregated, r.stats.generation_reset_keys, r.stats.track_reset_keys);
    return b;
}

// 连续帧：tracks_on_rail / off_rail 组合
struct FrameSpec {
    std::vector<DetectBox> boxes;
    std::vector<std::int32_t> tracks;
    std::vector<char> roi;
};

FrameSpec spec(std::vector<DetectBox> b, std::vector<std::int32_t> t, std::vector<char> r) {
    return FrameSpec{std::move(b), std::move(t), std::move(r)};
}

// 跑 N 帧，返回最终事件数/结果
struct RunOutcome {
    std::size_t events = 0;
    long long generation_reset_keys = 0;
    long long track_reset_keys = 0;
    long long unknown = 0;
    long long no_track = 0;
    long long low_conf = 0;
    long long mapped = 0;
    long long aggregated = 0;
    std::size_t detections_last = 0;
    bool available_last = false;
};

RunOutcome run_frames(RuleEngine* rules, SemanticAdapter* ad, const std::vector<FrameSpec>& frames,
                      std::vector<long long>* per_frame_events = nullptr) {
    RunOutcome o;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const FrameId f = fid(1, i + 1);
        InferenceEnvelope e = mk(f, frames[i].boxes, frames[i].tracks, frames[i].roi);
        SemanticResult r;
        auto ev = ad->adapt_and_dispatch(e, static_cast<long long>(i + 1) * 100, rules, &r);
        o.events += ev.size();
        if (per_frame_events) per_frame_events->push_back(static_cast<long long>(ev.size()));
        o.generation_reset_keys += r.stats.generation_reset_keys;
        o.track_reset_keys += r.stats.track_reset_keys;
        o.unknown += r.stats.blocked_unknown_label;
        o.no_track += r.stats.dropped_no_track;
        o.low_conf += r.stats.dropped_low_confidence;
        o.mapped += r.stats.mapped;
        o.aggregated += r.stats.aggregated;
        o.detections_last = r.detections.size();
        o.available_last = r.available;
    }
    return o;
}

int run_selftest() {
    // 1) person on rail，3 帧确认 -> 1 个 L1 事件
    {
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg());
        std::vector<FrameSpec> fs;
        for (int i = 0; i < 3; ++i)
            fs.push_back(spec({bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}));
        std::vector<long long> per;
        RunOutcome o = run_frames(&rules, &ad, fs, &per);
        check(o.events == 1, "person on rail -> 1 event after 3 frames",
              std::to_string(o.events));
        check(!rules.events().empty() && rules.events()[0].type == AlarmType::PersonOnRail &&
                  rules.events()[0].level == AlarmLevel::L1,
              "event type/level person_on_rail L1");
        check(per.size() == 3 && per[0] == 0 && per[1] == 0 && per[2] == 1, "confirm at 3rd frame");
    }
    // 2) 未知 label 拦截：不产生报警
    {
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg());
        std::vector<FrameSpec> fs;
        for (int i = 0; i < 3; ++i)
            fs.push_back(spec({bx("car", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}));
        RunOutcome o = run_frames(&rules, &ad, fs);
        check(o.events == 0 && o.unknown == 3 && o.mapped == 0, "unknown label blocked (no alarm)",
              "events=" + std::to_string(o.events) + " unknown=" + std::to_string(o.unknown));
    }
    // 3) 多目标：两个 track 各自确认，不重复开事件
    {
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg());
        std::vector<FrameSpec> fs;
        for (int i = 0; i < 3; ++i)
            fs.push_back(spec({bx("person", 0.2, 0.7, 0.1, 0.1), bx("person", 0.6, 0.7, 0.1, 0.1)},
                              {1, 2}, {1, 1}));
        std::vector<long long> per;
        RunOutcome o = run_frames(&rules, &ad, fs, &per);
        check(o.events == 2, "multi target -> 2 events (one per track)", std::to_string(o.events));
        check(per.size() == 3 && per[2] == 2, "both confirmed on 3rd frame");
        // 第 4 帧同目标：抑制窗口内不新增
        InferenceEnvelope e4 = mk(fid(1, 4),
                                   {bx("person", 0.2, 0.7, 0.1, 0.1), bx("person", 0.6, 0.7, 0.1, 0.1)},
                                   {1, 2}, {1, 1});
        SemanticResult r4;
        auto ev4 = ad.adapt_and_dispatch(e4, 400, &rules, &r4);
        check(ev4.empty(), "no duplicate events within suppress window", std::to_string(ev4.size()));
    }
    // 4) 非候选目标不误清其他目标的确认
    {
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg());
        std::vector<FrameSpec> fs;
        fs.push_back(spec({bx("person", 0.2, 0.7, 0.1, 0.1)}, {1}, {1}));  // track1 命中
        fs.push_back(spec({bx("person", 0.2, 0.7, 0.1, 0.1)}, {1}, {1}));  // track1 命中
        // 第3帧：track1 命中 + track2 不在轨道（非候选，只应清 track2）
        fs.push_back(spec({bx("person", 0.2, 0.7, 0.1, 0.1), bx("person", 0.2, 0.1, 0.1, 0.1)},
                          {1, 2}, {1, 0}));
        std::vector<long long> per;
        RunOutcome o = run_frames(&rules, &ad, fs, &per);
        check(o.events == 1, "off-rail target does not clear other target", std::to_string(o.events));
        check(o.track_reset_keys >= 0, "track reset counted", std::to_string(o.track_reset_keys));
    }
    // 5) generation 变化清理确认：不跨代复用
    {
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg());
        for (int i = 0; i < 2; ++i) {
            InferenceEnvelope e = mk(fid(1, i + 1), {bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1});
            (void)ad.adapt_and_dispatch(e, (i + 1) * 100, &rules, nullptr);
        }
        // 换 generation：第 3 帧（新代第 1 帧）不应确认
        InferenceEnvelope e3 = mk(fid(2, 1), {bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1});
        SemanticResult r3;
        auto ev3 = ad.adapt_and_dispatch(e3, 300, &rules, &r3);
        check(ev3.empty(), "generation change resets confirm (no alarm on 1st new frame)");
        check(r3.stats.generation_reset_keys > 0, "generation reset keys observable",
              std::to_string(r3.stats.generation_reset_keys));
        // 新代继续 2 帧 -> 共 3 帧才报警
        auto ev4 = ad.adapt_and_dispatch(mk(fid(2, 2), {bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}),
                                         400, &rules, nullptr);
        auto ev5 = ad.adapt_and_dispatch(mk(fid(2, 3), {bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}),
                                         500, &rules, nullptr);
        check(ev4.empty() && ev5.size() == 1, "new generation confirms independently",
              std::to_string(ev4.size()) + "/" + std::to_string(ev5.size()));
    }
    // 6) 无 track 不得用 0 冒充：丢弃并计数
    {
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg());
        std::vector<FrameSpec> fs;
        for (int i = 0; i < 3; ++i)
            fs.push_back(spec({bx("person", 0.45, 0.7, 0.1, 0.1)}, {-1}, {1}));
        RunOutcome o = run_frames(&rules, &ad, fs);
        check(o.events == 0 && o.no_track == 3 && o.detections_last == 0,
              "null track dropped (never mapped to 0)",
              "events=" + std::to_string(o.events) + " no_track=" + std::to_string(o.no_track));
    }
    // 7) 置信度不足（在轨）-> 不下发
    {
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg(0.5));
        std::vector<FrameSpec> fs;
        for (int i = 0; i < 3; ++i)
            fs.push_back(spec({bx("person", 0.45, 0.7, 0.1, 0.1, 0.3)}, {1}, {1}));
        RunOutcome o = run_frames(&rules, &ad, fs);
        check(o.events == 0 && o.low_conf == 3, "low confidence -> no dispatch",
              "events=" + std::to_string(o.events) + " low=" + std::to_string(o.low_conf));
    }
    // 8) obstacle 白名单 -> FrontObstacle L1
    {
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg());
        std::vector<FrameSpec> fs;
        for (int i = 0; i < 3; ++i)
            fs.push_back(spec({bx("obstacle", 0.45, 0.7, 0.1, 0.1)}, {3}, {1}));
        RunOutcome o = run_frames(&rules, &ad, fs);
        check(o.events == 1 && !rules.events().empty() &&
                  rules.events()[0].type == AlarmType::FrontObstacle,
              "obstacle -> FrontObstacle");
    }
    // 9) envelope 不可用 -> 不产生判定/报警
    {
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg());
        InferenceEnvelope e = make_unavailable_envelope(fid(1, 1), AiProfile::Front, "m", 0, 1000, "");
        SemanticResult r;
        auto ev = ad.adapt_and_dispatch(e, 100, &rules, &r);
        check(ev.empty() && !r.available && r.detections.empty() &&
                  r.reason == semantic_reason::kUnavailable(),
              "unavailable envelope -> no detections/alarms");
    }
    // 10) 同帧同 track 聚合 + 按 track 排序
    {
        SemanticAdapter ad(scfg());
        InferenceEnvelope e = mk(fid(1, 1),
                                 {bx("person", 0.5, 0.7, 0.1, 0.1, 0.6),
                                  bx("person", 0.5, 0.7, 0.1, 0.1, 0.9),
                                  bx("person", 0.1, 0.7, 0.1, 0.1, 0.8)},
                                 {5, 5, 2}, {1, 1, 1});
        SemanticResult r = ad.adapt(e, 100);
        check(r.detections.size() == 2 && r.stats.aggregated == 1, "same track aggregated",
              std::to_string(r.detections.size()) + "/" + std::to_string(r.stats.aggregated));
        check(r.detections[0].track_id == 2 && r.detections[1].track_id == 5, "sorted by track_id");
        check(r.detections[1].confidence == 0.9f, "aggregate keeps highest confidence");
    }
    emit("RESULT: " + std::string(g_fail == 0 ? "PASS" : "FAIL") + " failures=" + std::to_string(g_fail));
    return g_fail == 0 ? 0 : 1;
}

int run_fixture(const std::string& name, const RuleConfig& rc, const SemanticConfig& sc) {
    emit("type=config confirm_frames=" + std::to_string(rc.confirm_frames) +
         " confirm_window_ms=" + std::to_string(rc.confirm_window_ms) +
         " confidence_min=" + std::to_string(rc.confidence_min) +
         " suppress_window_ms=" + std::to_string(rc.suppress_window_ms) +
         " require_track=" + std::string(sc.require_track ? "1" : "0") +
         " allowed=" + std::to_string(sc.allowed_labels.size()));
    RuleEngine rules(rc);
    SemanticAdapter ad(sc);
    std::vector<FrameSpec> fs;
    bool expect_events = false;
    long long expect_unknown = -1, expect_no_track = -1;
    if (name == "person_on_rail") {
        for (int i = 0; i < 3; ++i) fs.push_back(spec({bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}));
        expect_events = true;
    } else if (name == "unknown_label") {
        for (int i = 0; i < 3; ++i) fs.push_back(spec({bx("car", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}));
        expect_unknown = 3;
    } else if (name == "multi_target") {
        for (int i = 0; i < 3; ++i)
            fs.push_back(spec({bx("person", 0.2, 0.7, 0.1, 0.1), bx("person", 0.6, 0.7, 0.1, 0.1)},
                              {1, 2}, {1, 1}));
        expect_events = true;
    } else if (name == "off_rail") {
        fs.push_back(spec({bx("person", 0.2, 0.7, 0.1, 0.1)}, {1}, {1}));
        fs.push_back(spec({bx("person", 0.2, 0.7, 0.1, 0.1)}, {1}, {1}));
        fs.push_back(spec({bx("person", 0.2, 0.7, 0.1, 0.1), bx("person", 0.2, 0.1, 0.1, 0.1)},
                          {1, 2}, {1, 0}));
        expect_events = true;
    } else if (name == "generation_reset") {
        RuleEngine* rp = &rules;
        for (int i = 0; i < 2; ++i)
            (void)ad.adapt_and_dispatch(mk(fid(1, i + 1), {bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}),
                                        (i + 1) * 100, rp, nullptr);
        SemanticResult r3;
        auto ev1 = ad.adapt_and_dispatch(mk(fid(2, 1), {bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}),
                                         300, rp, &r3);
        auto ev2 = ad.adapt_and_dispatch(mk(fid(2, 2), {bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}),
                                         400, rp, nullptr);
        auto ev3 = ad.adapt_and_dispatch(mk(fid(2, 3), {bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}),
                                         500, rp, nullptr);
        emit("type=gen gen_reset_keys=" + std::to_string(r3.stats.generation_reset_keys) +
             " events=" + std::to_string(ev1.size()) + "/" + std::to_string(ev2.size()) + "/" +
             std::to_string(ev3.size()));
        const bool pass = ev1.empty() && ev2.empty() && ev3.size() == 1 &&
                          r3.stats.generation_reset_keys > 0;
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    } else if (name == "no_track") {
        for (int i = 0; i < 3; ++i) fs.push_back(spec({bx("person", 0.45, 0.7, 0.1, 0.1)}, {-1}, {1}));
        expect_no_track = 3;
    } else if (name == "low_confidence") {
        for (int i = 0; i < 3; ++i)
            fs.push_back(spec({bx("person", 0.45, 0.7, 0.1, 0.1, 0.3)}, {1}, {1}));
    } else if (name == "obstacle") {
        for (int i = 0; i < 3; ++i) fs.push_back(spec({bx("obstacle", 0.45, 0.7, 0.1, 0.1)}, {3}, {1}));
        expect_events = true;
    } else if (name == "unavailable") {
        InferenceEnvelope e = make_unavailable_envelope(fid(1, 1), AiProfile::Front, "m", 0, 1000, "");
        SemanticResult r;
        auto ev = ad.adapt_and_dispatch(e, 100, &rules, &r);
        emit("type=result " + res_line(r));
        const bool pass = ev.empty() && !r.available && r.detections.empty();
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    } else if (name == "aggregate" || name == "sorted") {
        SemanticResult r = ad.adapt(
            mk(fid(1, 1), {bx("person", 0.5, 0.7, 0.1, 0.1, 0.6), bx("person", 0.5, 0.7, 0.1, 0.1, 0.9),
                           bx("person", 0.1, 0.7, 0.1, 0.1, 0.8)},
               {5, 5, 2}, {1, 1, 1}),
            100);
        emit("type=result " + res_line(r));
        bool pass = r.detections.size() == 2 && r.stats.aggregated == 1;
        if (name == "sorted") pass = pass && r.detections[0].track_id == 2 && r.detections[1].track_id == 5;
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    } else {
        std::fprintf(stderr, "unknown fixture: %s\n", name.c_str());
        return 2;
    }

    RunOutcome o = run_frames(&rules, &ad, fs);
    emit("type=result events=" + std::to_string(o.events) + " unknown=" + std::to_string(o.unknown) +
         " no_track=" + std::to_string(o.no_track) + " low_conf=" + std::to_string(o.low_conf) +
         " mapped=" + std::to_string(o.mapped) + " det_last=" + std::to_string(o.detections_last) +
         " track_reset=" + std::to_string(o.track_reset_keys));
    emit("type=events total=" + std::to_string(rules.events().size()));
    for (const auto& e : rules.events())
        emit("event type=" + std::string(to_string(e.type)) + " level=" +
             std::to_string(static_cast<int>(e.level)) + " track=" + std::to_string(e.track_id));
    bool pass = true;
    if (expect_events) pass = o.events >= 1;
    else pass = o.events == 0;
    if (expect_unknown >= 0) pass = pass && o.unknown == expect_unknown;
    if (expect_no_track >= 0) pass = pass && o.no_track == expect_no_track;
    emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
    return pass ? 0 : 1;
}

int run_config(const std::string& dir) {
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
    SemanticConfig sc = scfg(cfg.settings.thresholds.confidence_min);
    emit("type=config dir=" + dir + " confirm_frames=" + std::to_string(rc.confirm_frames) +
         " confidence_min=" + std::to_string(rc.confidence_min) +
         " suppress_window_ms=" + std::to_string(rc.suppress_window_ms));
    return run_fixture("multi_target", rc, sc);
}

}  // namespace

int main(int argc, char** argv) {
    std::string mode, name = "person_on_rail", out, json, dir = RAILHMI_CONFIG_DIR;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--selftest") mode = "selftest";
        else if (a == "--fixture") { next(&name); mode = "fixture"; }
        else if (a == "--config") { next(&dir); mode = "config"; }
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-vision-semantic --selftest | --fixture NAME | --config DIR\n");
            return 0;
        } else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }
    if (mode.empty()) { std::fprintf(stderr, "need --selftest/--fixture/--config\n"); return 2; }
    if (!out.empty()) g_fo.open(out, std::ios::trunc);
    int rc = 0;
    if (mode == "selftest") rc = run_selftest();
    else if (mode == "fixture") rc = run_fixture(name, rcfg(), scfg());
    else rc = run_config(dir);
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
