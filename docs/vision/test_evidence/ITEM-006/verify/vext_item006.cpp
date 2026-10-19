// vext_item006 — 验证自有扩展夹具（software_tester.software_verification，
// VERIFY-20261019-item006-v1）。断言交付 core-tests/selftest 未覆盖的验收边界。
//
// 链接交付 build-core/src/core/librailhmi_core.a，直连 vision_inference_envelope 行为面。
// 判例命名 g##_；期望全部离线自查后落笔（FRAME_GATE/BoundedQueue 语义重推导）。

#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/frame_identity.h"
#include "railhmi/core/vision_inference_envelope.h"

using namespace railhmi::core;

namespace {

int g_fail = 0;

void check(bool ok, const std::string& name, const std::string& detail = "") {
    std::printf("vext %s %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(),
                detail.empty() ? "" : " :: ", detail.c_str());
    if (!ok) ++g_fail;
}

FrameId fid(const std::string& s, std::uint64_t gen, std::uint64_t seq) {
    FrameId f;
    f.session_id = s;
    f.camera_id = "CAM-01";
    f.generation = gen;
    f.seq = seq;
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

std::vector<std::uint8_t> g_mask_buf;

MaskRef mk_mask(int box_index) {
    g_mask_buf.assign(64 * 48, 1);
    MaskRef m;
    m.box_index = box_index;
    m.width = 64;
    m.height = 48;
    m.stride_bytes = 64;
    m.plane = &g_mask_buf;
    m.kind = "rail";
    return m;
}

InferenceEnvelope env(std::uint64_t gen, std::uint64_t seq, bool with_box = true,
                      AiAvailability avail = AiAvailability::Ready,
                      const std::string& reason = "READY") {
    static const std::vector<DetectBox> boxes = {person(0.4, 0.7, 0.1, 0.1)};
    static const std::vector<MaskRef> masks = {mk_mask(0)};
    EnvelopeBuildInput in;
    in.frame = fid("s-vext", gen, seq);
    in.profile = AiProfile::Front;
    in.model_version = "mv-ext";
    in.capture_mono_us = 2000000;
    in.result_mono_us = 2005000;
    in.detect_ready = (avail == AiAvailability::Ready);
    in.seg_ready = (avail == AiAvailability::Ready);
    in.requires_segmentation = true;
    in.unavailable_reason = reason;
    in.boxes = (with_box && avail == AiAvailability::Ready) ? &boxes : nullptr;
    in.masks = (with_box && avail == AiAvailability::Ready) ? &masks : nullptr;
    if (in.boxes) in.track_ids = {9};
    InferenceEnvelope e = build_envelope(in);
    e.availability = avail;
    return e;
}

void test_gate_generation_edges() {
    const std::string s = "s-vext";
    // g01 世代前进：同 session 更大 generation 被接受，且 seq 基线重置（旧小 seq 不再乱序）
    {
        EnvelopePublisher p(s, 8);
        std::string r;
        check(p.submit(env(1, 50), &r), "g01_gen1_seq50_accepted", r);
        check(p.submit(env(2, 0), &r), "g01_gen2_seq0_resets_baseline", r);
        check(p.stats().accepted == 2 && p.stats().rejected == 0, "g01_two_accepted_no_reject",
              envelope_stats_json(p.stats()));
    }
    // g02 空构造 publisher：首个 submit 采纳 session（FrameGate 契约）
    {
        EnvelopePublisher p("s-vext", 8);
        std::string r;
        check(p.submit(env(0, 1), &r), "g02_first_submit_adopts_session", r);
        InferenceEnvelope other = env(0, 2);
        other.frame.session_id = "other";
        check(!p.submit(other, &r) && r == "stale_session", "g02_second_session_rejected", r);
    }
    // g03 旧代拒绝后同代新 seq 仍被接受（拒绝不破坏 gate 状态）
    {
        EnvelopePublisher p(s, 8);
        std::string r;
        check(p.submit(env(3, 10), &r), "g03_seed_gen3", r);
        check(!p.submit(env(2, 11), &r) && r == "stale_generation", "g03_stale_gen_rejected", r);
        check(p.submit(env(3, 11), &r), "g03_same_gen_new_seq_still_accepted", r);
        check(p.stats().stale_generation == 1 && p.stats().accepted == 2,
              "g03_counts_after_mixed", envelope_stats_json(p.stats()));
    }
    // g04 乱序与重复的判定边界：seq 倒退=乱序；等值=重复；前进=接受
    {
        EnvelopePublisher p(s, 8);
        std::string r;
        (void)p.submit(env(1, 100), &r);
        check(!p.submit(env(1, 100), &r) && r == "duplicate_seq", "g04_equal_seq_duplicate", r);
        check(!p.submit(env(1, 99), &r) && r == "out_of_order_seq", "g04_lower_seq_out_of_order", r);
        check(!p.submit(env(1, 98), &r) && r == "out_of_order_seq", "g04_much_lower_still_ooo", r);
        check(p.submit(env(1, 101), &r), "g04_higher_seq_accepted", r);
    }
}

void test_latency_edges() {
    // g05 latency 边界：999µs→0（整除截断）、1000µs→1、恰好 0、负→0
    check(latency_ms_of(1000000, 1000999) == 0, "g05_999us_truncates_to_0");
    check(latency_ms_of(1000000, 1001000) == 1, "g05_1000us_is_1ms");
    check(latency_ms_of(0, 0) == 0, "g05_zero_zero");
    check(latency_ms_of(5000000, 4999000) == 0, "g05_negative_clamped");
    // g06 大延迟（含排队）：1.5s 队列等待 + 0.5s 推理 = 2000ms
    InferenceEnvelope e = env(1, 1);
    e.capture_mono_us = 1000000;
    e.result_mono_us = 3000000;
    e.latency_ms = latency_ms_of(e.capture_mono_us, e.result_mono_us);
    check(e.latency_ms == 2000, "g06_queue_wait_included_in_latency",
          std::to_string(e.latency_ms));
}

void test_queue_edges() {
    const std::string s = "s-vext";
    // g07 depth=1：逐出全部旧项，只保留最新
    {
        EnvelopePublisher p(s, 1);
        std::string r;
        for (std::uint64_t q = 1; q <= 5; ++q) (void)p.submit(env(1, q), &r);
        check(p.queue_size() == 1 && p.queue_capacity() == 1, "g07_depth1_size", std::to_string(p.queue_size()));
        check(p.stats().queue_dropped == 4, "g07_depth1_dropped_4", std::to_string(p.stats().queue_dropped));
        InferenceEnvelope a;
        check(p.pop(&a) && a.frame.seq == 5, "g07_depth1_keeps_newest", std::to_string(a.frame.seq));
    }
    // g08 push 恒真语义：超载 push 不拒绝（非阻塞丢最旧），submit 返回 true
    {
        EnvelopePublisher p(s, 2);
        std::string r;
        const bool ok1 = p.submit(env(1, 1), &r);
        const bool ok2 = p.submit(env(1, 2), &r);
        const bool ok3 = p.submit(env(1, 3), &r);
        check(ok1 && ok2 && ok3, "g08_push_always_true_under_load");
        check(p.queue_size() == 2 && p.stats().queue_dropped == 1, "g08_drop_oldest_counted",
              envelope_stats_json(p.stats()));
    }
    // g09 unavailable envelope 也入队（可用语义不影响发布通路），分类计数
    {
        EnvelopePublisher p(s, 8);
        std::string r;
        check(p.submit(env(1, 1, false, AiAvailability::Unavailable, "AI_UNAVAILABLE"), &r),
              "g09_unavailable_still_enqueued", r);
        check(p.submit(env(1, 2, false, AiAvailability::Ready, "READY"), &r),
              "g09_empty_ready_enqueued", r);
        EnvelopeStats st = p.stats();
        check(st.unavailable == 1 && st.empty_boxes == 1 && st.non_empty == 0,
              "g09_classification_counts", envelope_stats_json(st));
    }
    // g10 popped/queue_dropped 计数一致性
    {
        EnvelopePublisher p(s, 2);
        std::string r;
        for (std::uint64_t q = 1; q <= 4; ++q) (void)p.submit(env(1, q), &r);
        InferenceEnvelope a;
        (void)p.pop(&a);
        (void)p.pop(&a);
        // 4 push 入 depth2：push3 弹 seq1、push4 弹 seq2 → dropped=2（离线重推导）
        check(p.stats().popped == 2 && p.queue_dropped() == 2 && p.stats().queue_dropped == 2,
              "g10_popped_and_dropped_consistent",
              "popped=" + std::to_string(p.stats().popped) + " dropped=" + std::to_string(p.queue_dropped()));
        check(!p.pop(&a) && p.stats().popped == 2, "g10_empty_pop_no_count");
    }
}

void test_json_and_reason() {
    const std::string s = "s-vext";
    // g11 track_id -1 在 JSON 原样输出（不冒充 0/null 字符串）
    {
        InferenceEnvelope e = env(1, 1, true);
        e.boxes[0].track_id = -1;
        const std::string j = envelope_json(e);
        check(j.find("\"track_id\":-1") != std::string::npos, "g11_json_track_id_minus_one");
        check(j.find("\"track_id\":0") == std::string::npos, "g11_json_no_track_id_zero");
    }
    // g12 reason 覆盖：自定义 token 原样携带；默认 token 三种稳定值
    {
        InferenceEnvelope e = env(1, 1, false, AiAvailability::Unavailable, "THERMAL_GUARD");
        check(e.reason == "THERMAL_GUARD", "g12_reason_override_custom", e.reason);
        InferenceEnvelope d = make_unavailable_envelope(fid(s, 1, 2), AiProfile::Front, "mv", 0, 1, "");
        check(d.reason == envelope_reason::kAiUnavailable(), "g12_default_ai_unavailable", d.reason);
        check(std::string(envelope_reason::kReady()) == "READY"
                  && std::string(envelope_reason::kSegUnavailable()) == "SEG_UNAVAILABLE",
              "g12_default_tokens_stable");
        const std::string j = envelope_json(d);
        check(j.find("\"reason\":\"AI_UNAVAILABLE\"") != std::string::npos, "g12_json_reason", d.reason);
    }
    // g13 空 boxes 与 unavailable 的 JSON 在 availability/reason 上可区分
    {
        InferenceEnvelope er = env(1, 3, false, AiAvailability::Ready, "READY");
        er.availability = AiAvailability::Ready;
        er.reason = envelope_reason::kReady();
        InferenceEnvelope eu = make_unavailable_envelope(fid(s, 1, 4), AiProfile::Front, "mv", 0, 1, "");
        const std::string jr = envelope_json(er);
        const std::string ju = envelope_json(eu);
        check(jr.find("\"availability\":\"ready\"") != std::string::npos
                  && ju.find("\"availability\":\"unavailable\"") != std::string::npos,
              "g13_json_availability_distinct");
        check(jr != ju, "g13_json_outputs_differ");
    }
    // g14 无 boxes 指针的 ready 构造：masks 也不复制（框/掩码对齐空）
    {
        EnvelopeBuildInput in;
        in.frame = fid(s, 1, 5);
        in.profile = AiProfile::Front;
        in.detect_ready = true;
        in.seg_ready = true;
        in.requires_segmentation = false;
        in.boxes = nullptr;
        in.masks = nullptr;
        InferenceEnvelope e = build_envelope(in);
        check(e.ready() && e.boxes.empty() && e.masks.empty(),
              "g14_no_pointer_ready_empty_both");
    }
    // g15 track_ids 短表：缺省段填 -1 不冒 0
    {
        static const std::vector<DetectBox> boxes = {person(0.1, 0.1, 0.1, 0.1),
                                                     person(0.2, 0.2, 0.1, 0.1),
                                                     person(0.3, 0.3, 0.1, 0.1)};
        EnvelopeBuildInput in;
        in.frame = fid(s, 1, 6);
        in.profile = AiProfile::Front;
        in.boxes = &boxes;
        in.track_ids = {5};  // 只有第一项有 track
        InferenceEnvelope e = build_envelope(in);
        check(e.boxes.size() == 3 && e.boxes[0].track_id == 5 && e.boxes[1].track_id == -1
                  && e.boxes[2].track_id == -1,
              "g15_track_ids_partial_fill_minus_one");
    }
    // g16 stats JSON 字段齐备（含三类拒绝键）
    {
        const std::string sj = envelope_stats_json(EnvelopeStats{});
        check(sj.find("\"stale_session\":0") != std::string::npos
                  && sj.find("\"stale_generation\":0") != std::string::npos
                  && sj.find("\"duplicate_seq\":0") != std::string::npos
                  && sj.find("\"out_of_order_seq\":0") != std::string::npos
                  && sj.find("\"queue_dropped\":0") != std::string::npos,
              "g16_stats_json_all_counters");
    }
}

}  // namespace

int main() {
    test_gate_generation_edges();
    test_latency_edges();
    test_queue_edges();
    test_json_and_reason();
    std::printf("vext %s (%d failure%s)\n", g_fail == 0 ? "OK" : "FAILED",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
