// Verifier-owned ext fixture for ITEM-019 protected clips / gaps (software_verification).
// Linked against the delivered librailhmi_core.a (+ railhmi_platform) built by the
// verifier's own board_build.py TestRun round; asserts acceptance boundaries the
// implementer's tests/probe do not cover:
//   * configurable pre/post windows (non-default values) & buffer depth derivation
//   * RollingBuffer: capacity-0 unbounded, set_capacity shrink, drop_before closed
//     boundary, count_in closed interval, unknown camera buffer() == nullptr
//   * clip lifecycle: finalize exactly at t1 (half-open post window), idempotent
//     finalize, multiple clips finalize together, alarm on frame-less camera
//   * frame counting under realistic push ordering with a small custom window
//   * on_frame window trimming drops frames older than pre+post
//   * estimate_clip_size exact bytes at fixed bitrate
//   * retention via ClipRecorder: policy applied AND recorder state updated
//     (normal rows removed, protected evicted oldest-first only when over capacity,
//     age eviction of continuous only)
//   * gaps via ClipRecorder: t0/t1 swap, adjacency (<=1ms) merge, bridging merge,
//     first-existing non-empty reason, empty reason replaced, cross-camera sort
//   * manifest JSON: all declared keys, state recording/finalized, level string
//   * FileStore atomic write: overwrite replaces content, no leftover .tmp files,
//     failure path leaves target absent with err set
// Pure logic + stdlib, no Qt — like the delivered probe.
#include <cstdio>
#include <dirent.h>
#include <string>
#include <vector>

#include "railhmi/core/clip.h"
#include "railhmi/platform/file_store.h"

using namespace railhmi::core;
using railhmi::platform::FileStore;

namespace {

int g_passed = 0, g_failed = 0;
std::vector<std::string> g_notes;

void check(const std::string& name, bool ok) {
    (ok ? g_passed : g_failed) += 1;
    if (!ok) g_notes.push_back("FAIL: " + name);
}

bool has_id(const std::vector<long long>& ids, long long id) {
    for (long long v : ids) if (v == id) return true;
    return false;
}

const long long kGiB = 1024LL * 1024LL * 1024LL;
const long long kDayMs = 86400000LL;

// --- 1. configuration & rolling buffer -------------------------------------
void t_config_and_buffer() {
    ClipConfig cfg;
    cfg.pre_seconds = 5;
    cfg.post_seconds = 10;
    ClipRecorder rec(cfg, RetentionPolicy{});
    check("configurable pre/post -> duration 15s",
          rec.on_alarm("CAM-01", AlarmLevel::L1, 60000).duration_ms() == 15000);
    check("buffer depth derived = fps*(pre+post)+25",
          rec.config().max_frames_per_channel ==
              static_cast<std::size_t>(25.0 * 15.0) + 25);

    ClipConfig cfg2;
    cfg2.max_frames_per_channel = 123;
    ClipRecorder rec2(cfg2, RetentionPolicy{});
    check("explicit max_frames respected (not overwritten)",
          rec2.config().max_frames_per_channel == 123);

    RollingBuffer b0(0);
    for (int i = 0; i < 100; ++i) b0.push(i);
    check("capacity 0 behaves unbounded", b0.size() == 100);

    RollingBuffer b(3);
    for (int i = 0; i < 10; ++i) b.push(i);
    b.set_capacity(2);
    check("set_capacity shrink trims to new cap",
          b.size() == 2 && b.earliest() == 8 && b.latest() == 9);

    RollingBuffer b2(4);
    b2.push(70); b2.push(80); b2.push(90);
    b2.drop_before(80);
    check("drop_before keeps t == boundary (closed)", b2.earliest() == 80);
    b2.drop_before(91);
    check("drop_before past latest empties buffer", b2.empty());

    RollingBuffer b3(4);
    b3.push(10); b3.push(20); b3.push(30);
    check("count_in single closed point", b3.count_in(20, 20) == 1);
    check("count_in inside window", b3.count_in(15, 25) == 1);
    check("count_in full closed range", b3.count_in(10, 30) == 3);
    check("count_in outside range", b3.count_in(31, 40) == 0);

    ClipRecorder rec3(ClipConfig{}, RetentionPolicy{});
    check("unknown camera buffer() nullptr before any frame",
          rec3.buffer("CAM-X") == nullptr);
}

// --- 2. clip lifecycle ------------------------------------------------------
void t_clip_lifecycle() {
    ClipRecorder rec(ClipConfig{}, RetentionPolicy{});
    (void)rec.on_alarm("CAM-01", AlarmLevel::L1, 60000);
    rec.finalize(120000);
    check("alarm on frame-less camera -> finalize ok, frames=0",
          rec.clips()[0].complete && rec.clips()[0].frame_count == 0);
    const std::size_t n1 = rec.finalize(999999);
    check("finalize idempotent (returns 0 second time)", n1 == 0);

    ClipRecorder r2(ClipConfig{}, RetentionPolicy{});
    const ProtectedClip a = r2.on_alarm("CAM-01", AlarmLevel::L1, 60000);
    const ProtectedClip b = r2.on_alarm("CAM-01", AlarmLevel::L2, 100000);
    check("two alarms -> distinct ids, both recorded",
          a.id != b.id && r2.clips().size() == 2);
    check("finalize(t1-1) not yet", r2.finalize(119999) == 0);
    check("finalize exactly t1 completes", r2.finalize(120000) == 1);
    check("finalize multiple clips together", r2.finalize(160000) == 1);
    check("finalize count monotonic (no double count)",
          r2.finalize(99999999) == 0 && r2.clips().size() == 2);

    // custom small window (pre=2, post=3, fps=25): alarm at 5000 -> [3000,8000].
    // Pushes continue to exactly t1: the trim threshold of the last push is
    // t1-(pre+post)*1000 = 3000 = t0, so the whole [t0,t1] window survives and
    // frame_count must cover the complete pre window.
    ClipRecorder r3(ClipConfig{2, 3, 25.0, 0}, RetentionPolicy{});
    for (long long t = 0; t <= 5000; t += 100) r3.on_frame("CAM-01", t);
    r3.on_alarm("CAM-01", AlarmLevel::L1, 5000);
    for (long long t = 5100; t <= 8000; t += 100) r3.on_frame("CAM-01", t);
    r3.finalize(8000);
    check("pre=2/post=3 window: t0=3000 t1=8000 duration 5000",
          r3.clips()[0].t0_ms == 3000 && r3.clips()[0].t1_ms == 8000 &&
          r3.clips()[0].duration_ms() == 5000);
    check("frame_count at finalize(now==t1) covers full [t0,t1] = 51 frames",
          r3.clips()[0].frame_count == 51);
    // pushing past t1 cannot change an already-finalized clip.
    for (long long t = 8100; t <= 13000; t += 100) r3.on_frame("CAM-01", t);
    check("finalized clip frame_count stable after later pushes",
          r3.clips()[0].frame_count == 51);

    // default window buffer-trim check: on_frame(130000) drops everything
    // before 130000 - 90s = 40000.
    ClipRecorder r4(ClipConfig{}, RetentionPolicy{});
    for (long long t = 0; t <= 130000; t += 40) r4.on_frame("CAM-01", t);
    check("on_frame trims older than pre+post (nothing before 40000)",
          r4.buffer("CAM-01")->count_in(0, 39999) == 0);
    check("on_frame keeps window start boundary (40000..40240 kept)",
          r4.buffer("CAM-01")->count_in(40000, 40040) == 2);

    // estimate size at default 30/60 window (90s), 4 Mbps exact.
    ClipRecorder r5(ClipConfig{}, RetentionPolicy{});
    ProtectedClip big;
    big.t0_ms = 0;
    big.t1_ms = 90000;
    check("estimate_clip_size 90s@4Mbps = 45MB", estimate_clip_size(big, 4.0) == 45000000);
    check("estimate_clip_size zero duration = 0", estimate_clip_size(ProtectedClip{}, 4.0) == 0);
}

// --- 3. retention through ClipRecorder (state mutation) ---------------------
void t_retention_recorder() {
    // 3 protected 5GiB clips, capacity 12GiB: oldest critical evicted once.
    ClipRecorder r1(ClipConfig{}, RetentionPolicy{3650, 12.0});
    r1.on_alarm("CAM-01", AlarmLevel::L1, 10000);
    r1.on_alarm("CAM-01", AlarmLevel::L1, 20000);
    r1.on_alarm("CAM-01", AlarmLevel::L1, 30000);
    for (long long id = 1; id <= 3; ++id) r1.set_clip_size(id, 5 * kGiB);
    const RetentionResult res1 = r1.apply_retention_now(40000);
    check("over-capacity: oldest protected evicted",
          res1.evicted_ids.size() == 1 && has_id(res1.evicted_ids, 1) &&
          res1.evicted_critical == 1);
    check("over-capacity: recorder state updated (2 clips, 2 segments)",
          r1.clips().size() == 2 && r1.segments().size() == 2);
    check("remaining protected are the newer ones",
          r1.clips()[0].id == 2 && r1.clips()[1].id == 3);

    // 2 normal 8GiB + 1 protected 1GiB, capacity 10GiB: oldest normal evicted.
    ClipRecorder r2(ClipConfig{}, RetentionPolicy{3650, 10.0});
    r2.add_normal_segment(101, "CAM-02", 0, 1000, 8 * kGiB);
    r2.add_normal_segment(102, "CAM-03", 0, 1000, 8 * kGiB);
    r2.on_alarm("CAM-01", AlarmLevel::L1, 60000);
    r2.set_clip_size(1, kGiB);
    const RetentionResult res2 = r2.apply_retention_now(70000);
    check("normal evicted first under capacity",
          res2.evicted_continuous == 1 && res2.evicted_critical == 0 &&
          has_id(res2.evicted_ids, 101));
    check("normal row removed from recorder after retention",
          r2.segments().size() == 2 && r2.clips().size() == 1);
    bool kept102 = false, kept1 = false;
    for (const auto& s : r2.segments()) {
        if (s.id == 102 && s.kind == SegmentKind::Continuous) kept102 = true;
        if (s.id == 1 && s.kind == SegmentKind::Critical) kept1 = true;
    }
    check("retained ids are 102 (continuous) and 1 (critical)", kept102 && kept1);

    // age eviction: continuous older than retention_days evicted; critical not.
    ClipRecorder r3(ClipConfig{}, RetentionPolicy{1, 1000.0});
    const long long now = 10 * kDayMs;
    r3.add_normal_segment(201, "CAM-02", 0, now - 2 * kDayMs, 1);
    r3.add_normal_segment(202, "CAM-02", 0, now - 3 * kDayMs, 1);
    r3.add_normal_segment(203, "CAM-02", 0, now, 1);
    r3.on_alarm("CAM-01", AlarmLevel::L1, now - 2 * kDayMs);
    const RetentionResult res3 = r3.apply_retention_now(now);
    check("age evicts stale continuous only",
          res3.evicted_continuous == 2 && has_id(res3.evicted_ids, 201) &&
          has_id(res3.evicted_ids, 202) && !has_id(res3.evicted_ids, 203) &&
          res3.evicted_critical == 0);
    check("age: critical not evicted, fresh continuous kept",
          r3.clips().size() == 1 && r3.segments().size() == 2);

    // under capacity -> nothing evicted, state unchanged.
    ClipRecorder r4(ClipConfig{}, RetentionPolicy{3650, 12.0});
    r4.add_normal_segment(301, "CAM-02", 0, 1000, kGiB);
    r4.on_alarm("CAM-01", AlarmLevel::L1, 60000);
    const RetentionResult res4 = r4.apply_retention_now(70000);
    check("under capacity nothing evicted",
          res4.evicted_ids.empty() && r4.segments().size() == 2);
}

// --- 4. gaps through ClipRecorder -------------------------------------------
void t_gaps() {
    ClipRecorder rec(ClipConfig{}, RetentionPolicy{});
    rec.on_recording_interrupt("CAM-01", 5000, 1000);
    check("swap t0/t1 normalized", rec.gaps().size() == 1 &&
          rec.gaps()[0].t0_ms == 1000 && rec.gaps()[0].t1_ms == 5000);

    ClipRecorder r2(ClipConfig{}, RetentionPolicy{});
    r2.on_recording_interrupt("CAM-01", 1000, 2000);
    r2.on_recording_interrupt("CAM-01", 2001, 3000);
    check("adjacent (<=1ms) merged", r2.gaps().size() == 1 &&
          r2.gaps()[0].t0_ms == 1000 && r2.gaps()[0].t1_ms == 3000);

    ClipRecorder r3(ClipConfig{}, RetentionPolicy{});
    r3.on_recording_interrupt("CAM-01", 0, 10);
    r3.on_recording_interrupt("CAM-01", 20, 30);
    check("two separate gaps before bridge", r3.gaps().size() == 2);
    r3.on_recording_interrupt("CAM-01", 5, 25);
    check("bridging new gap absorbs both", r3.gaps().size() == 1 &&
          r3.gaps()[0].t0_ms == 0 && r3.gaps()[0].t1_ms == 30);

    ClipRecorder r4(ClipConfig{}, RetentionPolicy{});
    r4.on_recording_interrupt("CAM-01", 0, 10, "a");
    r4.on_recording_interrupt("CAM-01", 5, 15, "b");
    check("first existing non-empty reason wins",
          r4.gaps().size() == 1 && r4.gaps()[0].reason == "a");

    ClipRecorder r5(ClipConfig{}, RetentionPolicy{});
    r5.on_recording_interrupt("CAM-01", 0, 10, "");
    r5.on_recording_interrupt("CAM-01", 5, 15, "new");
    check("empty existing reason replaced by new",
          r5.gaps().size() == 1 && r5.gaps()[0].reason == "new");

    ClipRecorder r6(ClipConfig{}, RetentionPolicy{});
    r6.on_recording_interrupt("CAM-03", 0, 10);
    r6.on_recording_interrupt("CAM-01", 0, 10);
    r6.on_recording_interrupt("CAM-02", 0, 10);
    check("cross-camera gaps sorted by cam, separate rows",
          r6.gaps().size() == 3 && r6.gaps()[0].cam == "CAM-01" &&
          r6.gaps()[1].cam == "CAM-02" && r6.gaps()[2].cam == "CAM-03");
}

// --- 5. manifest JSON ---------------------------------------------------------
void t_manifest() {
    ClipConfig cfg;
    cfg.pre_seconds = 30;
    cfg.post_seconds = 60;
    ClipRecorder rec(cfg, RetentionPolicy{});
    rec.on_frame("CAM-04", 60000);
    const ProtectedClip c = rec.on_alarm("CAM-04", AlarmLevel::L2, 60000);
    const std::string pre = clip_manifest_json(c);
    check("manifest recording state before finalize",
          pre.find("\"state\":\"recording\"") != std::string::npos &&
          pre.find("\"complete\":false") != std::string::npos);
    const char* keys[] = {"\"id\"", "\"cam\"", "\"level\"", "\"trigger_ms\"",
                          "\"t0_ms\"", "\"t1_ms\"", "\"duration_ms\"", "\"state\"",
                          "\"complete\"", "\"frame_count\""};
    bool all_keys = true;
    for (const char* k : keys)
        if (pre.find(k) == std::string::npos) all_keys = false;
    check("manifest carries all declared keys", all_keys);
    check("manifest level string L2", pre.find("\"level\":\"L2\"") != std::string::npos);
    check("manifest cam echo", pre.find("\"cam\":\"CAM-04\"") != std::string::npos);
    check("manifest duration = t1-t0", pre.find("\"duration_ms\":90000") != std::string::npos);
    rec.finalize(c.t1_ms);
    const std::string post = clip_manifest_json(rec.clips()[0]);
    check("manifest finalized after post window",
          post.find("\"state\":\"finalized\"") != std::string::npos &&
          post.find("\"complete\":true") != std::string::npos);
}

// --- 6. atomic file write edges -----------------------------------------------
void t_atomic_edges() {
    const std::string dir = "/tmp/railhmi_clip_ext";
    const std::string p1 = dir + "/ov.json";
    std::string err;
    check("ensure parent creates chain", FileStore::ensure_parent_dir(p1, &err));
    check("atomic write #1", FileStore::write_atomic(p1, "AAA", &err));
    check("read back #1", FileStore::read_all(p1, &err) == "AAA");
    check("atomic overwrite replaces content", FileStore::write_atomic(p1, "BBB", &err));
    check("read back after overwrite", FileStore::read_all(p1, &err) == "BBB");

    bool tmp_left = false;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n.find(".tmp.") != std::string::npos) tmp_left = true;
        }
        closedir(d);
    }
    check("no leftover .tmp.<pid> files after atomic writes", !tmp_left);

    const std::string blocker = dir + "/blocker";
    FileStore::write_atomic(blocker, "x", &err);
    const std::string p2 = blocker + "/c.json";
    err.clear();
    const bool wrote_bad = FileStore::write_atomic(p2, "y", &err);
    check("write under regular file fails", !wrote_bad && !err.empty());
    check("failed write leaves target absent", !FileStore::exists(p2));
    check("failed write leaves blocker unchanged",
          FileStore::read_all(blocker, &err) == "x");
}

}  // namespace

int main() {
    t_config_and_buffer();
    t_clip_lifecycle();
    t_retention_recorder();
    t_gaps();
    t_manifest();
    t_atomic_edges();

    for (const auto& n : g_notes) std::printf("%s\n", n.c_str());
    const bool ok = g_failed == 0;
    std::printf("== %d passed, %d failed ==\n", g_passed, g_failed);
    std::printf("RESULT: %s passed=%d failed=%d\n", ok ? "PASS" : "FAIL", g_passed, g_failed);
    return ok ? 0 : 1;
}
