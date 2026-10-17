// railhmi-core 帧身份与跨重连代际测试（视频 ITEM-003 / TC-V-SEQ-01..02、TC-V-LEASE 前置）。
// 验证：seq 同代严格递增、generation 仅在重建时递增且 seq 归零、
// 消费者接纳器拒绝旧 session/generation/重复/乱序，时间字段语义。

#include "test_util.h"

#include <string>

#include "railhmi/core/frame_identity.h"

namespace {

using namespace railhmi::core;

void test_seq_strictly_increasing() {
    FrameSequencer s("sess-A", "CAM-01");
    s.begin_generation();
    FrameMeta a = s.next(1000, 1700000000000LL, 123);
    FrameMeta b = s.next(2000, 1700000000000LL, 456);
    FrameMeta c = s.next(3000, -1, -1);
    railhmi_test::check(a.id.seq == 1 && b.id.seq == 2 && c.id.seq == 3,
                        "seq starts at 1 and strictly increases");
    railhmi_test::check(a.id.generation == 1, "first generation is 1");
    railhmi_test::check(b.capture_mono_us > a.capture_mono_us, "mono us increasing");
    railhmi_test::check(a.capture_wall_ms == 1700000000000LL && a.pts_ns == 123,
                        "capture_wall_ms/pts_ns carried");
    railhmi_test::check(c.capture_wall_ms == -1 && c.pts_ns == -1,
                        "unavailable wall/pts are -1 (not fake values)");
    railhmi_test::check(a.id.camera_id == "CAM-01" && a.id.session_id == "sess-A",
                        "camera/session carried");
    railhmi_test::check(a.id.same_stream(b.id) &&
                            !a.id.same_stream(FrameId{"sess-A", "CAM-01", 2, 1}),
                        "same_stream ignores seq but not generation");
}

void test_generation_rebuild_resets_seq() {
    FrameSequencer s("sess-A", "CAM-01");
    s.begin_generation();
    s.next(1, -1, -1);
    s.next(2, -1, -1);
    railhmi_test::check(s.generation() == 1 && s.last_seq() == 2, "gen1 seq=2 before rebuild");
    const std::uint64_t g = s.begin_generation();
    railhmi_test::check(g == 2, "begin_generation increments to 2");
    FrameMeta m = s.next(3, -1, -1);
    railhmi_test::check(m.id.generation == 2 && m.id.seq == 1,
                        "rebuild resets seq to 1 in new generation");
}

void test_gate_in_order_and_duplicates() {
    FrameGate gate("sess-A");
    FrameId a{"sess-A", "CAM-01", 1, 1};
    FrameId b{"sess-A", "CAM-01", 1, 2};
    FrameId dup{"sess-A", "CAM-01", 1, 2};
    FrameId back{"sess-A", "CAM-01", 1, 1};
    railhmi_test::check(gate.admit(a) == FrameAdmission::Accepted, "in-order first accepted");
    railhmi_test::check(gate.admit(b) == FrameAdmission::Accepted, "in-order next accepted");
    railhmi_test::check(gate.admit(dup) == FrameAdmission::DuplicateSeq, "duplicate seq rejected");
    railhmi_test::check(gate.admit(back) == FrameAdmission::OutOfOrderSeq, "out-of-order seq rejected");
    railhmi_test::check(gate.last_seq() == 2 && gate.generation() == 1,
                        "gate state unchanged by rejects");
}

void test_gate_stale_generation_and_session() {
    FrameGate gate("sess-A");
    FrameId g1{"sess-A", "CAM-01", 1, 5};
    FrameId g2a{"sess-A", "CAM-01", 2, 1};
    FrameId g2b{"sess-A", "CAM-01", 2, 2};
    FrameId stale{"sess-A", "CAM-01", 1, 6};       // 旧代际到达
    FrameId other_session{"sess-B", "CAM-01", 2, 3};
    railhmi_test::check(gate.admit(g1) == FrameAdmission::Accepted, "gen1 accepted");
    railhmi_test::check(gate.admit(g2a) == FrameAdmission::Accepted, "new generation accepted");
    railhmi_test::check(gate.admit(g2b) == FrameAdmission::Accepted, "gen2 seq2 accepted");
    railhmi_test::check(gate.admit(stale) == FrameAdmission::StaleGeneration,
                        "old generation rejected after rebuild");
    railhmi_test::check(gate.admit(other_session) == FrameAdmission::StaleSession,
                        "different session rejected");
    railhmi_test::check(gate.generation() == 2 && gate.last_seq() == 2,
                        "gate reflects newest generation");
}

void test_gate_adopts_first_session() {
    FrameGate gate;  // 未指定期望 session，采纳首帧
    FrameId a{"sess-X", "CAM-02", 1, 1};
    FrameId b{"sess-X", "CAM-02", 1, 2};
    FrameId c{"sess-Y", "CAM-02", 1, 3};
    railhmi_test::check(gate.admit(a) == FrameAdmission::Accepted, "adopt first session");
    railhmi_test::check(gate.session_id() == "sess-X", "session adopted");
    railhmi_test::check(gate.admit(b) == FrameAdmission::Accepted, "same session continues");
    railhmi_test::check(gate.admit(c) == FrameAdmission::StaleSession, "switch session rejected");
    railhmi_test::check(gate.is_current(a) && !gate.is_current(c), "is_current respects session");
}

void test_session_id_helper() {
    const std::string a = make_session_id();
    const std::string b = make_session_id();
    railhmi_test::check(!a.empty() && !b.empty() && a != b,
                        "make_session_id is non-empty and unique per call");
}

void test_admission_to_string() {
    railhmi_test::check(std::string(to_string(FrameAdmission::Accepted)) == "accepted" &&
                            std::string(to_string(FrameAdmission::StaleGeneration)) ==
                                "stale_generation" &&
                            std::string(to_string(FrameAdmission::DuplicateSeq)) ==
                                "duplicate_seq" &&
                            std::string(to_string(FrameAdmission::OutOfOrderSeq)) ==
                                "out_of_order_seq" &&
                            std::string(to_string(FrameAdmission::StaleSession)) == "stale_session",
                        "admission tokens stable");
}

}  // namespace

void run_frame_identity_tests() {
    test_seq_strictly_increasing();
    test_generation_rebuild_resets_seq();
    test_gate_in_order_and_duplicates();
    test_gate_stale_generation_and_session();
    test_gate_adopts_first_session();
    test_session_id_helper();
    test_admission_to_string();
}
