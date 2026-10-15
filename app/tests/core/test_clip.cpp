// railhmi-core 保护片段与缺口测试（ITEM-019 / DESKTOP §10、PLY-008）。
// 覆盖：有界滚动缓冲、报警固化 [pre,post] 时长、finalize 完整化、缺口合并、
// 保留策略保护片段优先、片段清单 JSON、原子写盘（临时文件 -> 重命名）。

#include "test_util.h"

#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/clip.h"
#include "railhmi/platform/file_store.h"

namespace {

using namespace railhmi::core;

void test_rolling_buffer() {
    RollingBuffer b(3);
    for (int i = 0; i < 10; ++i) b.push(i * 10);
    railhmi_test::check(b.size() == 3, "buffer bounded to capacity");
    railhmi_test::check(b.earliest() == 70 && b.latest() == 90, "oldest dropped");
    b.drop_before(80);
    railhmi_test::check(b.earliest() == 80, "drop_before trims");
    railhmi_test::check(b.count_in(80, 90) == 2, "count_in window");
}

void test_alarm_clip() {
    ClipConfig cfg;
    cfg.pre_seconds = 30;
    cfg.post_seconds = 60;
    ClipRecorder rec(cfg, RetentionPolicy{});
    for (long long t = 0; t <= 130000; t += 40) rec.on_frame("CAM-01", t);

    const ProtectedClip c = rec.on_alarm("CAM-01", AlarmLevel::L1, 60000);
    railhmi_test::check(c.t0_ms == 30000 && c.t1_ms == 120000, "clip range = trigger-pre .. trigger+post");
    railhmi_test::check(c.duration_ms() == 90000, "clip duration = (pre+post) seconds");
    railhmi_test::check(c.state == ClipState::Recording && !c.complete, "clip pending before post window");

    railhmi_test::check(rec.finalize(119000) == 0, "not finalized before t1");
    railhmi_test::check(rec.finalize(120000) == 1, "finalized at t1");
    railhmi_test::check(rec.clips()[0].complete && rec.clips()[0].state == ClipState::Finalized,
                        "clip complete after post window");
    railhmi_test::check(rec.clips()[0].frame_count > 0, "frame_count from rolling buffer");

    const std::string json = clip_manifest_json(rec.clips()[0]);
    railhmi_test::check(json.find("\"duration_ms\":90000") != std::string::npos, "manifest has duration");
    railhmi_test::check(json.find("\"cam\":\"CAM-01\"") != std::string::npos, "manifest has camera");
    railhmi_test::check(json.find("\"complete\":true") != std::string::npos, "manifest complete");
}

void test_gap_merge() {
    ClipRecorder rec(ClipConfig{}, RetentionPolicy{});
    rec.on_recording_interrupt("CAM-01", 1000, 3000, "recording_interrupted");
    rec.on_recording_interrupt("CAM-01", 2500, 6000, "recording_interrupted");
    railhmi_test::check(rec.gaps().size() == 1, "overlapping gaps merged");
    railhmi_test::check(rec.gaps()[0].t0_ms == 1000 && rec.gaps()[0].t1_ms == 6000, "merged range");
    railhmi_test::check(rec.gaps()[0].reason == "recording_interrupted", "gap reason kept");
    rec.on_recording_interrupt("CAM-02", 1000, 2000, "x");
    railhmi_test::check(rec.gaps().size() == 2, "different camera separate gap");
}

void test_retention_priority() {
    // 容量 12 GiB；两段普通各 8 GiB + 一个保护 1 GiB -> 淘汰普通最旧，保护保留。
    RetentionPolicy pol;
    pol.max_gb = 12.0;
    pol.retention_days = 3650;
    const long long g = 1024LL * 1024LL * 1024LL;
    RecordingSegmentRow n1; n1.id = 101; n1.cam = "CAM-01"; n1.t0_ms = 0; n1.t1_ms = 1000;
    n1.kind = SegmentKind::Continuous; n1.size_bytes = 8 * g;
    RecordingSegmentRow n2 = n1; n2.id = 102; n2.cam = "CAM-02";
    RecordingSegmentRow p1; p1.id = 1; p1.cam = "CAM-01"; p1.t0_ms = 20000; p1.t1_ms = 110000;
    p1.kind = SegmentKind::Critical; p1.size_bytes = 1 * g;
    const std::vector<RecordingSegmentRow> segs = {n1, n2, p1};

    const RetentionResult r = apply_retention(segs, 110000, pol);
    railhmi_test::check(r.evicted_continuous == 1 && r.evicted_critical == 0,
                        "normal evicted first, protected retained");
    bool protected_kept = true;
    for (long long id : r.evicted_ids) if (id == 1) protected_kept = false;
    railhmi_test::check(protected_kept, "protected clip id not evicted");

    // 容量极小 -> 保护也被淘汰（最旧优先）
    pol.max_gb = 0.5;
    const RetentionResult r2 = apply_retention(segs, 110000, pol);
    bool protected_evicted = false;
    for (long long id : r2.evicted_ids) if (id == 1) protected_evicted = true;
    railhmi_test::check(protected_evicted, "protected evicted only when still over capacity");
}

void test_atomic_write() {
    const std::string dir = "/tmp/railhmi_clip_unit";
    const std::string path = dir + "/clip-1.json";
    std::string err;
    const std::string content = "{\"id\":1,\"cam\":\"CAM-01\"}";
    railhmi_test::check(railhmi::platform::FileStore::ensure_parent_dir(path, &err), "ensure clip dir");
    railhmi_test::check(railhmi::platform::FileStore::write_atomic(path, content, &err), "atomic write clip");
    railhmi_test::check(railhmi::platform::FileStore::read_all(path, &err) == content, "clip content round trip");
    railhmi_test::check(railhmi::platform::FileStore::exists(path), "clip file exists");
    std::remove(path.c_str());
}

}  // namespace

void run_clip_tests() {
    test_rolling_buffer();
    test_alarm_clip();
    test_gap_merge();
    test_retention_priority();
    test_atomic_write();
}
