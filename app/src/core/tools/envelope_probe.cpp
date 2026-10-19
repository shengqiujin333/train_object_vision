// railhmi-vision-envelope (视觉推理 ITEM-006)
//
// 推理结果关联与发布探针：InferenceEnvelope 身份/时刻/延迟/可用性/模型版本/带 track_id 的 box
// 与掩码引用；旧 session/generation、重复/乱序 seq 分别计数；有界队列丢最旧；不可用与空框可区分。
//
// 用法:
//   railhmi-vision-envelope --selftest [--out log] [--json file]
//   railhmi-vision-envelope --fixture ready|empty|unavailable|seg_unavailable|stale|duplicate|
//       out_of_order|queue_overflow|no_track|identity
//   railhmi-vision-envelope --config DIR [--depth N]
//
// 退出码: 0 通过；1 失败；2 参数错误。

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/vision_inference_envelope.h"

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

FrameId fid(const std::string& s, std::uint64_t g, std::uint64_t q) {
    FrameId f;
    f.session_id = s;
    f.camera_id = "CAM-01";
    f.generation = g;
    f.seq = q;
    return f;
}

DetectBox person(double x, double y, double w, double h, double c = 0.9) {
    DetectBox b;
    b.x = static_cast<float>(x);
    b.y = static_cast<float>(y);
    b.w = static_cast<float>(w);
    b.h = static_cast<float>(h);
    b.score = static_cast<float>(c);
    b.class_id = 0;
    b.label = "person";
    return b;
}

std::vector<std::uint8_t> g_mask;

MaskRef rail_mask_ref() {
    g_mask.assign(static_cast<std::size_t>(64) * 48, 1);
    MaskRef m;
    m.box_index = 0;
    m.width = 64;
    m.height = 48;
    m.stride_bytes = 64;
    m.plane = &g_mask;
    m.kind = "rail";
    return m;
}

InferenceEnvelope ready_env(std::uint64_t seq, long long cap, long long res, bool with_box,
                            std::int32_t track_id) {
    static const std::vector<DetectBox> boxes = {person(0.45, 0.75, 0.1, 0.1)};
    static const std::vector<MaskRef> masks = {rail_mask_ref()};
    EnvelopeBuildInput in;
    in.frame = fid("s-env", 1, seq);
    in.profile = AiProfile::Front;
    in.model_version = "rknn-2.3.2/yolov5s";
    in.capture_mono_us = cap;
    in.result_mono_us = res;
    in.detect_ready = true;
    in.seg_ready = true;
    in.requires_segmentation = true;
    in.boxes = with_box ? &boxes : nullptr;
    in.masks = with_box ? &masks : nullptr;
    if (with_box) in.track_ids = {track_id};
    return build_envelope(in);
}

int run_selftest() {
    const std::string s = "s-env";
    // 延迟：完成 − 接收（含排队）
    check(latency_ms_of(1000000, 1025000) == 25, "latency = (result-capture)/1000");
    check(latency_ms_of(1000000, 999000) == 0, "latency negative clamped to 0");
    check(latency_ms_of(1000000, 1000000) == 0, "latency zero");

    // ready envelope：身份/时刻/延迟/版本/框/掩码
    {
        InferenceEnvelope e = ready_env(5, 1000000, 1033000, true, 7);
        check(e.ready() && e.reason == envelope_reason::kReady(), "ready envelope");
        check(e.frame.session_id == s && e.frame.camera_id == "CAM-01" && e.frame.generation == 1 &&
                  e.frame.seq == 5,
              "identity key preserved");
        check(e.capture_mono_us == 1000000 && e.result_mono_us == 1033000 && e.latency_ms == 33,
              "capture/result/latency");
        check(e.model_version == "rknn-2.3.2/yolov5s", "model_version carried");
        check(e.boxes.size() == 1 && e.boxes[0].track_id == 7 && e.boxes[0].label == "person",
              "box with track_id");
        check(e.masks.size() == 1 && e.masks[0].plane == &g_mask && e.masks[0].width == 64 &&
                  e.masks[0].box_index == 0,
              "mask ref preserved (zero-copy)");
    }
    // track_id 缺省必须是 null(-1)
    {
        InferenceEnvelope e = ready_env(6, 0, 1000, true, -1);
        check(e.boxes.size() == 1 && e.boxes[0].track_id == -1, "unknown track_id is null (-1)");
        check(e.boxes[0].track_id != 0, "track_id not fabricated as 0");
    }
    // 可用但无目标（空框）
    {
        InferenceEnvelope e = ready_env(7, 0, 1000, false, -1);
        check(e.ready() && e.empty_boxes() && e.reason == envelope_reason::kReady(),
              "ready + empty boxes (no target)");
    }
    // AI 不可用 -> unavailable + reason + 空框；与空框可区分
    {
        InferenceEnvelope e = make_unavailable_envelope(fid(s, 1, 8), AiProfile::Front,
                                                        "rknn-2.3.2/yolov5s", 0, 1000, "");
        check(!e.ready() && e.availability == AiAvailability::Unavailable &&
                  e.reason == envelope_reason::kAiUnavailable() && e.empty_boxes(),
              "unavailable + reason + empty boxes");
        InferenceEnvelope empty_ready = ready_env(9, 0, 1000, false, -1);
        check(e.availability != empty_ready.availability, "unavailable distinguishable from empty");
    }
    // 检测不可用 / 分割不可用（front 需要掩码）
    {
        EnvelopeBuildInput in;
        in.frame = fid(s, 1, 10);
        in.profile = AiProfile::Front;
        in.detect_ready = false;
        in.requires_segmentation = true;
        InferenceEnvelope e = build_envelope(in);
        check(!e.ready() && e.reason == envelope_reason::kAiUnavailable() && e.empty_boxes(),
              "detect not ready -> AI_UNAVAILABLE");
    }
    {
        EnvelopeBuildInput in;
        in.frame = fid(s, 1, 11);
        in.profile = AiProfile::Front;
        in.detect_ready = true;
        in.seg_ready = false;
        in.requires_segmentation = true;
        InferenceEnvelope e = build_envelope(in);
        check(!e.ready() && e.reason == envelope_reason::kSegUnavailable() && e.empty_boxes(),
              "seg not ready -> SEG_UNAVAILABLE");
    }
    // 距离 / in_roi 覆盖
    {
        static const std::vector<DetectBox> boxes = {person(0.1, 0.2, 0.1, 0.1)};
        EnvelopeBuildInput in;
        in.frame = fid(s, 1, 12);
        in.profile = AiProfile::Front;
        in.boxes = &boxes;
        in.track_ids = {3};
        in.distances_m = {42.5f};
        in.in_roi_flags = {1};
        InferenceEnvelope e = build_envelope(in);
        check(e.boxes.size() == 1 && e.boxes[0].track_id == 3 && e.boxes[0].distance_m == 42.5f &&
                  e.boxes[0].in_roi,
              "distance/in_roi overrides applied");
    }
    // 发布：接纳 + 逐类拒绝计数
    {
        EnvelopePublisher p(s, 8);
        std::string reason;
        check(p.submit(ready_env(20, 0, 1000, true, -1), &reason), "accepted first");
        check(!p.submit(ready_env(20, 0, 1000, true, -1), &reason) && reason == "duplicate_seq",
              "duplicate seq rejected", reason);
        check(!p.submit(ready_env(19, 0, 1000, true, -1), &reason) && reason == "out_of_order_seq",
              "out-of-order rejected", reason);
        EnvelopeBuildInput in;
        in.frame = fid(s, 0, 1);
        InferenceEnvelope old = build_envelope(in);
        check(!p.submit(old, &reason) && reason == "stale_generation", "stale generation rejected",
              reason);
        InferenceEnvelope other = ready_env(21, 0, 1000, true, -1);
        other.frame.session_id = "s-other";
        check(!p.submit(other, &reason) && reason == "stale_session", "stale session rejected", reason);
        EnvelopeStats st = p.stats();
        check(st.submitted == 5 && st.accepted == 1 && st.rejected == 4, "submit/accepted/rejected counts");
        check(st.duplicate_seq == 1 && st.out_of_order_seq == 1 && st.stale_generation == 1 &&
                  st.stale_session == 1,
              "per-class rejection counts independent",
              envelope_stats_json(st));
    }
    // 有界队列：超载丢最旧并计数，pop 得到最新保留项
    {
        EnvelopePublisher p(s, 2);
        for (std::uint64_t q = 30; q < 35; ++q) p.submit(ready_env(q, 0, 1000, true, -1));
        EnvelopeStats st = p.stats();
        check(p.queue_size() == 2 && p.queue_capacity() == 2, "queue bounded to depth",
              std::to_string(p.queue_size()));
        check(st.queued == 5 && st.queue_dropped == 3, "queue drop-oldest counted",
              envelope_stats_json(st));
        InferenceEnvelope a, b;
        check(p.pop(&a) && p.pop(&b) && a.frame.seq == 33 && b.frame.seq == 34,
              "queue keeps newest (pop order)",
              std::to_string(a.frame.seq) + "," + std::to_string(b.frame.seq));
        check(!p.pop(&a), "empty queue pop false");
        check(p.stats().popped == 2, "popped count");
    }
    // 统计分类：unavailable / empty_boxes / non_empty
    {
        EnvelopePublisher p(s, 8);
        p.submit(make_unavailable_envelope(fid(s, 1, 40), AiProfile::Front, "m", 0, 1000, ""), nullptr);
        p.submit(ready_env(41, 0, 1000, false, -1), nullptr);
        p.submit(ready_env(42, 0, 1000, true, -1), nullptr);
        EnvelopeStats st = p.stats();
        check(st.unavailable == 1 && st.empty_boxes == 1 && st.non_empty == 1,
              "availability/box classification counts", envelope_stats_json(st));
        check(st.queue_dropped >= 0, "drop counter non-negative");
    }
    // JSON：字段齐备且空框/不可用可区分
    {
        InferenceEnvelope r = ready_env(50, 1000000, 1025000, true, 9);
        InferenceEnvelope u = make_unavailable_envelope(fid(s, 1, 51), AiProfile::Front, "m", 0, 1000, "");
        const std::string rj = envelope_json(r);
        const std::string uj = envelope_json(u);
        check(rj.find("\"availability\":\"ready\"") != std::string::npos &&
                  rj.find("\"latency_ms\":25") != std::string::npos &&
                  rj.find("\"track_id\":9") != std::string::npos &&
                  rj.find("\"masks\"") != std::string::npos &&
                  rj.find("\"plane_present\":true") != std::string::npos,
              "ready json fields");
        check(uj.find("\"availability\":\"unavailable\"") != std::string::npos &&
                  uj.find("\"reason\":\"AI_UNAVAILABLE\"") != std::string::npos &&
                  uj.find("\"box_count\":0") != std::string::npos,
              "unavailable json fields");
        check(rj != uj && rj.find("\"availability\":\"ready\"") != std::string::npos &&
                  uj.find("\"availability\":\"unavailable\"") != std::string::npos,
              "empty vs unavailable distinguishable in json");
    }
    emit("RESULT: " + std::string(g_fail == 0 ? "PASS" : "FAIL") + " failures=" + std::to_string(g_fail));
    return g_fail == 0 ? 0 : 1;
}

int run_fixture(const std::string& name, int depth) {
    const std::string s = "s-env";
    emit("type=config queue_depth=" + std::to_string(depth));
    if (name == "ready") {
        InferenceEnvelope e = ready_env(1, 1000000, 1033000, true, 7);
        emit(envelope_json(e));
        const bool pass = e.ready() && e.boxes.size() == 1 && e.masks.size() == 1 &&
                          e.latency_ms == 33 && e.boxes[0].track_id == 7;
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    }
    if (name == "empty") {
        InferenceEnvelope e = ready_env(1, 0, 1000, false, -1);
        emit(envelope_json(e));
        const bool pass = e.ready() && e.empty_boxes();
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    }
    if (name == "unavailable") {
        InferenceEnvelope e = make_unavailable_envelope(fid(s, 1, 1), AiProfile::Front, "m", 0, 1000, "");
        emit(envelope_json(e));
        const bool pass = !e.ready() && e.reason == envelope_reason::kAiUnavailable() && e.empty_boxes();
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    }
    if (name == "seg_unavailable") {
        EnvelopeBuildInput in;
        in.frame = fid(s, 1, 1);
        in.profile = AiProfile::Front;
        in.detect_ready = true;
        in.seg_ready = false;
        in.requires_segmentation = true;
        InferenceEnvelope e = build_envelope(in);
        emit(envelope_json(e));
        const bool pass = !e.ready() && e.reason == envelope_reason::kSegUnavailable() && e.empty_boxes();
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    }
    if (name == "no_track") {
        InferenceEnvelope e = ready_env(1, 0, 1000, true, -1);
        emit(envelope_json(e));
        const bool pass = e.boxes.size() == 1 && e.boxes[0].track_id == -1;
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    }
    if (name == "identity" || name == "stale" || name == "duplicate" || name == "out_of_order") {
        EnvelopePublisher p(s, depth);
        std::string reason;
        (void)p.submit(ready_env(20, 0, 1000, true, -1), &reason);
        emit("accepted seq=20");
        if (name == "identity" || name == "duplicate") {
            (void)p.submit(ready_env(20, 0, 1000, true, -1), &reason);
            emit("duplicate -> " + reason);
        }
        if (name == "identity" || name == "out_of_order") {
            (void)p.submit(ready_env(19, 0, 1000, true, -1), &reason);
            emit("out_of_order -> " + reason);
        }
        if (name == "identity" || name == "stale") {
            EnvelopeBuildInput in;
            in.frame = fid(s, 0, 1);
            (void)p.submit(build_envelope(in), &reason);
            emit("stale_generation -> " + reason);
        }
        emit("stats " + envelope_stats_json(p.stats()));
        const EnvelopeStats st = p.stats();
        bool pass = true;
        if (name == "duplicate") pass = st.duplicate_seq == 1 && st.accepted == 1;
        else if (name == "out_of_order") pass = st.out_of_order_seq == 1 && st.accepted == 1;
        else if (name == "stale") pass = st.stale_generation == 1 && st.accepted == 1;
        else pass = st.accepted == 1 && st.duplicate_seq == 1 && st.out_of_order_seq == 1 &&
                    st.stale_generation == 1;
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    }
    if (name == "queue_overflow") {
        EnvelopePublisher p(s, depth);
        for (std::uint64_t q = 1; q <= static_cast<std::uint64_t>(depth + 3); ++q)
            p.submit(ready_env(q, 0, 1000, true, -1));
        emit("stats " + envelope_stats_json(p.stats()));
        const std::size_t size_before = p.queue_size();
        InferenceEnvelope a;
        p.pop(&a);
        emit("pop_first_seq=" + std::to_string(a.frame.seq));
        const EnvelopeStats st = p.stats();
        const bool pass = st.queued == static_cast<long long>(depth + 3) &&
                          st.queue_dropped == 3 && size_before == static_cast<std::size_t>(depth) &&
                          a.frame.seq == 4;
        emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
        return pass ? 0 : 1;
    }
    std::fprintf(stderr, "unknown fixture: %s\n", name.c_str());
    return 2;
}

int run_config(const std::string& dir, int depth_override) {
    ConfigLoadResult cfg = load_config_dir(dir);
    if (!cfg.ok) {
        emit("type=error config invalid");
        for (const auto& e : cfg.errors) emit("  " + e.str());
        return 1;
    }
    const int depth = depth_override > 0 ? depth_override : cfg.settings.app.ai.queue_depth;
    emit("type=config dir=" + dir + " app.ai.queue_depth=" +
         std::to_string(cfg.settings.app.ai.queue_depth) + " depth_used=" + std::to_string(depth) +
         (depth_override > 0 ? " (cli override)" : " (from app.ai.queue_depth)"));
    return run_fixture("queue_overflow", depth);
}

}  // namespace

int main(int argc, char** argv) {
    std::string mode, name = "ready", out, json, dir = RAILHMI_CONFIG_DIR;
    int depth = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--selftest") mode = "selftest";
        else if (a == "--fixture") { next(&name); mode = "fixture"; }
        else if (a == "--config") { next(&dir); mode = "config"; }
        else if (a == "--depth") { std::string v; next(&v); depth = std::atoi(v.c_str()); }
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-vision-envelope --selftest | --fixture NAME | --config DIR "
                        "[--depth N]\n");
            return 0;
        } else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }
    if (mode.empty()) { std::fprintf(stderr, "need --selftest/--fixture/--config\n"); return 2; }
    if (!out.empty()) g_fo.open(out, std::ios::trunc);
    int rc = 0;
    if (mode == "selftest") rc = run_selftest();
    else if (mode == "fixture") rc = run_fixture(name, depth > 0 ? depth : 2);
    else rc = run_config(dir, depth);
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
