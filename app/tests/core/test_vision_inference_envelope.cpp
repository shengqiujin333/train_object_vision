// railhmi-core 推理结果关联与发布测试（视觉推理 ITEM-006 纯逻辑）。
// 覆盖 TC-T-PUB-01..05：身份链/延迟/可用与空框/逐类拒绝计数/有界队列丢旧/JSON。

#include <string>
#include <vector>

#include "test_util.h"

#include "railhmi/core/vision_inference_envelope.h"

namespace {

using namespace railhmi::core;

FrameId fid(const std::string& s, std::uint64_t g, std::uint64_t q) {
    FrameId f;
    f.session_id = s;
    f.camera_id = "CAM-01";
    f.generation = g;
    f.seq = q;
    return f;
}

DetectBox person() {
    DetectBox b;
    b.x = 0.4f;
    b.y = 0.7f;
    b.w = 0.1f;
    b.h = 0.1f;
    b.score = 0.9f;
    b.class_id = 0;
    b.label = "person";
    return b;
}

std::vector<std::uint8_t> g_plane;

InferenceEnvelope mk_env(std::uint64_t seq, bool with_box, long long cap = 0, long long res = 1000,
                        std::int32_t track_id = -1) {
    static const std::vector<DetectBox> boxes = {person()};
    g_plane.assign(100, 1);
    static MaskRef m;
    m.box_index = 0;
    m.width = 10;
    m.height = 10;
    m.stride_bytes = 10;
    m.plane = &g_plane;
    static const std::vector<MaskRef> masks = {m};
    EnvelopeBuildInput in;
    in.frame = fid("s", 1, seq);
    in.profile = AiProfile::Front;
    in.model_version = "m1";
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

void test_latency() {
    railhmi_test::check(latency_ms_of(1000000, 1025000) == 25, "latency 25ms");
    railhmi_test::check(latency_ms_of(1000000, 1000000) == 0, "latency 0");
    railhmi_test::check(latency_ms_of(1000000, 999000) == 0, "latency negative clamped");
}

void test_envelope_fields() {
    InferenceEnvelope e = mk_env(5, true, 1000000, 1033000, 5);
    railhmi_test::check(e.ready() && e.reason == envelope_reason::kReady(), "ready envelope");
    railhmi_test::check(e.frame.session_id == "s" && e.frame.camera_id == "CAM-01" &&
                            e.frame.generation == 1 && e.frame.seq == 5,
                        "identity key");
    railhmi_test::check(e.latency_ms == 33 && e.capture_mono_us == 1000000 &&
                            e.result_mono_us == 1033000,
                        "capture/result/latency");
    railhmi_test::check(e.model_version == "m1" && e.boxes.size() == 1 && e.boxes[0].track_id == 5,
                        "model_version + track_id");
    railhmi_test::check(e.masks.size() == 1 && e.masks[0].plane == &g_plane && e.masks[0].box_index == 0,
                        "mask ref (zero-copy)");
    InferenceEnvelope nt = mk_env(6, true);
    railhmi_test::check(nt.boxes.size() == 1 && nt.boxes[0].track_id == -1, "track_id null default");
    railhmi_test::check(nt.boxes[0].track_id != 0, "track_id never fabricated 0");
    InferenceEnvelope empty = mk_env(7, false);
    railhmi_test::check(empty.ready() && empty.empty_boxes(), "ready + empty boxes");
}

void test_availability_semantics() {
    InferenceEnvelope u = make_unavailable_envelope(fid("s", 1, 8), AiProfile::Front, "m1", 0, 1000, "");
    railhmi_test::check(!u.ready() && u.reason == envelope_reason::kAiUnavailable() && u.empty_boxes(),
                        "unavailable + reason + empty boxes");
    InferenceEnvelope e = mk_env(9, false);
    railhmi_test::check(u.availability != e.availability, "unavailable vs empty distinguishable");
    {
        EnvelopeBuildInput in;
        in.frame = fid("s", 1, 10);
        in.profile = AiProfile::Front;
        in.detect_ready = false;
        in.requires_segmentation = true;
        InferenceEnvelope d = build_envelope(in);
        railhmi_test::check(!d.ready() && d.reason == envelope_reason::kAiUnavailable(),
                            "detect not ready -> AI_UNAVAILABLE");
    }
    {
        EnvelopeBuildInput in;
        in.frame = fid("s", 1, 11);
        in.profile = AiProfile::Front;
        in.seg_ready = false;
        in.requires_segmentation = true;
        InferenceEnvelope d = build_envelope(in);
        railhmi_test::check(!d.ready() && d.reason == envelope_reason::kSegUnavailable() &&
                                d.empty_boxes(),
                            "seg not ready -> SEG_UNAVAILABLE");
    }
    {
        static const std::vector<DetectBox> boxes = {person()};
        EnvelopeBuildInput in;
        in.frame = fid("s", 1, 12);
        in.profile = AiProfile::Front;
        in.boxes = &boxes;
        in.track_ids = {3};
        in.distances_m = {42.5f};
        in.in_roi_flags = {1};
        InferenceEnvelope d = build_envelope(in);
        railhmi_test::check(d.boxes.size() == 1 && d.boxes[0].track_id == 3 &&
                                d.boxes[0].distance_m == 42.5f && d.boxes[0].in_roi,
                            "distance/in_roi overrides");
    }
}

void test_gate_counts() {
    EnvelopePublisher p("s", 8);
    std::string reason;
    railhmi_test::check(p.submit(mk_env(20, true), &reason), "accepted");
    railhmi_test::check(!p.submit(mk_env(20, true), &reason) && reason == "duplicate_seq",
                        "duplicate rejected");
    railhmi_test::check(!p.submit(mk_env(19, true), &reason) && reason == "out_of_order_seq",
                        "out-of-order rejected");
    {
        EnvelopeBuildInput in;
        in.frame = fid("s", 0, 1);
        railhmi_test::check(!p.submit(build_envelope(in), &reason) && reason == "stale_generation",
                            "stale generation rejected");
    }
    {
        InferenceEnvelope other = mk_env(21, true);
        other.frame.session_id = "other";
        railhmi_test::check(!p.submit(other, &reason) && reason == "stale_session",
                            "stale session rejected");
    }
    const EnvelopeStats st = p.stats();
    railhmi_test::check(st.submitted == 5 && st.accepted == 1 && st.rejected == 4, "submit counts");
    railhmi_test::check(st.duplicate_seq == 1 && st.out_of_order_seq == 1 && st.stale_generation == 1 &&
                            st.stale_session == 1,
                        "per-class counts independent");
}

void test_bounded_queue() {
    EnvelopePublisher p("s", 2);
    for (std::uint64_t q = 30; q < 35; ++q) p.submit(mk_env(q, true));
    railhmi_test::check(p.queue_size() == 2 && p.queue_capacity() == 2, "queue bounded");
    const EnvelopeStats st = p.stats();
    railhmi_test::check(st.queued == 5 && st.queue_dropped == 3, "drop-oldest counted");
    InferenceEnvelope a, b;
    railhmi_test::check(p.pop(&a) && p.pop(&b) && a.frame.seq == 33 && b.frame.seq == 34,
                        "keeps newest");
    railhmi_test::check(!p.pop(&a), "empty pop false");
    railhmi_test::check(p.stats().popped == 2, "popped count");
    railhmi_test::check(p.stats().queue_dropped == p.queue_dropped(), "drop counter consistent");
}

void test_classification_counts() {
    EnvelopePublisher p("s", 8);
    p.submit(make_unavailable_envelope(fid("s", 1, 40), AiProfile::Front, "m", 0, 1000, ""), nullptr);
    p.submit(mk_env(41, false), nullptr);
    p.submit(mk_env(42, true), nullptr);
    const EnvelopeStats st = p.stats();
    railhmi_test::check(st.unavailable == 1 && st.empty_boxes == 1 && st.non_empty == 1,
                        "unavailable/empty/non-empty counts");
}

void test_json() {
    InferenceEnvelope r = mk_env(50, true, 1000000, 1025000, 5);
    InferenceEnvelope u = make_unavailable_envelope(fid("s", 1, 51), AiProfile::Front, "m", 0, 1000, "");
    const std::string rj = envelope_json(r);
    const std::string uj = envelope_json(u);
    railhmi_test::check(rj.find("\"availability\":\"ready\"") != std::string::npos &&
                            rj.find("\"latency_ms\":25") != std::string::npos &&
                            rj.find("\"track_id\":5") != std::string::npos &&
                            rj.find("\"masks\"") != std::string::npos,
                        "ready json fields");
    railhmi_test::check(uj.find("\"availability\":\"unavailable\"") != std::string::npos &&
                            uj.find("\"reason\":\"AI_UNAVAILABLE\"") != std::string::npos &&
                            uj.find("\"box_count\":0") != std::string::npos,
                        "unavailable json fields");
    railhmi_test::check(rj != uj, "empty vs unavailable json differs");
    const std::string sj = envelope_stats_json(EnvelopeStats{});
    railhmi_test::check(sj.find("\"queue_dropped\"") != std::string::npos, "stats json fields");
}

}  // namespace

void run_vision_inference_envelope_tests() {
    test_latency();
    test_envelope_fields();
    test_availability_semantics();
    test_gate_counts();
    test_bounded_queue();
    test_classification_counts();
    test_json();
}
