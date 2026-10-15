// railhmi-core/platform 持久化与保留策略测试（ITEM-007 / TC-CORE-06）。
// 时间基准、保留淘汰顺序、缺口合并始终测试；
// SQLite 仓储（9 表读写、写入-重启-读取、WAL 重放、保留执行、缺口查询）在 RAILHMI_HAVE_SQLITE 时测试。

#include "test_util.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/core/persistence_model.h"
#include "railhmi/core/retention.h"
#include "railhmi/core/timebase.h"
#ifdef RAILHMI_HAVE_SQLITE
#include "railhmi/platform/repository.h"
#endif

namespace {

using namespace railhmi::core;

double bytes_to_gb(long long bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0); }

void test_timebase() {
    TimeBase tb;
    railhmi_test::check(!tb.calibrated(), "timebase starts uncalibrated");
    tb.calibrate(1000, 5000000);
    railhmi_test::check(tb.calibrated(), "timebase calibrated");
    railhmi_test::check(tb.to_wall_ms(5000000) == 1000, "to_wall_ms at reference");
    railhmi_test::check(tb.to_wall_ms(7000000) == 3000, "to_wall_ms +2s");
    railhmi_test::check(tb.to_mono_us(3000) == 7000000, "to_mono_us round-trip");
    railhmi_test::check(tb.to_wall_ms(12345678) == 8345, "to_wall_ms truncates to ms");
    // 往返在毫秒对齐的单调值上恒等（毫秒分辨率）
    const long long mono_ms_aligned = 5000000 + 7345000;  // 偏移为整毫秒
    railhmi_test::check(tb.to_mono_us(tb.to_wall_ms(mono_ms_aligned)) == mono_ms_aligned,
                        "wall/mono round-trip identity (ms-aligned)");
    railhmi_test::check(TimeBase::system_now_ms() > 1600000000000LL, "system_now_ms is UTC epoch ms");
    railhmi_test::check(TimeBase::steady_now_us() > 0, "steady_now_us positive");
}

void test_retention_age() {
    long long now = 100LL * 86400000LL;  // day 100
    std::vector<RecordingSegmentRow> segs;
    RecordingSegmentRow s1; s1.id = 1; s1.kind = SegmentKind::Continuous; s1.t1_ms = now - 2 * 86400000LL; s1.size_bytes = 10;
    RecordingSegmentRow s2; s2.id = 2; s2.kind = SegmentKind::Continuous; s2.t1_ms = now - 3600000LL; s2.size_bytes = 10;
    RecordingSegmentRow s3; s3.id = 3; s3.kind = SegmentKind::Critical;   s3.t1_ms = now - 2 * 86400000LL; s3.size_bytes = 10;
    segs = {s1, s2, s3};
    RetentionPolicy p; p.retention_days = 1; p.max_gb = 1000.0;
    RetentionResult r = apply_retention(segs, now, p);
    railhmi_test::check(r.evicted_ids.size() == 1 && r.evicted_ids[0] == 1,
                        "age: only expired continuous evicted");
    railhmi_test::check(r.retained_bytes == 20, "age: retained bytes 20");
    railhmi_test::check(r.evicted_critical == 0, "age: critical not evicted");
    railhmi_test::check(r.evicted_continuous == 1, "age: one continuous evicted");
}

void test_retention_capacity() {
    long long now = 100LL * 86400000LL;
    std::vector<RecordingSegmentRow> segs;
    RecordingSegmentRow co; co.id = 11; co.kind = SegmentKind::Continuous; co.t0_ms = 100; co.t1_ms = 200; co.size_bytes = 10;
    RecordingSegmentRow cn; cn.id = 12; cn.kind = SegmentKind::Continuous; cn.t0_ms = 200; cn.t1_ms = 300; cn.size_bytes = 10;
    RecordingSegmentRow ko; ko.id = 13; ko.kind = SegmentKind::Critical;   ko.t0_ms = 50;  ko.t1_ms = 150; ko.size_bytes = 10;
    RecordingSegmentRow kn; kn.id = 14; kn.kind = SegmentKind::Critical;   kn.t0_ms = 150; kn.t1_ms = 250; kn.size_bytes = 10;
    segs = {co, cn, ko, kn};

    {   // limit 30 -> 淘汰最旧普通片段 1 个
        RetentionPolicy p; p.retention_days = 1000; p.max_gb = bytes_to_gb(30);
        RetentionResult r = apply_retention(segs, now, p);
        railhmi_test::check(r.evicted_ids.size() == 1 && r.evicted_ids[0] == 11,
                            "capacity: oldest continuous evicted first");
        railhmi_test::check(r.evicted_critical == 0 && r.retained_bytes == 30,
                            "capacity: keep critical, retained 30");
    }
    {   // limit 10 -> 淘汰两个普通 + 最旧保护
        RetentionPolicy p; p.retention_days = 1000; p.max_gb = bytes_to_gb(10);
        RetentionResult r = apply_retention(segs, now, p);
        railhmi_test::check(r.evicted_ids.size() == 3 &&
                                r.evicted_ids[0] == 11 && r.evicted_ids[1] == 12 && r.evicted_ids[2] == 13,
                            "capacity: continuous(11,12) then oldest critical(13)");
        railhmi_test::check(r.evicted_continuous == 2 && r.evicted_critical == 1,
                            "capacity: eviction counts");
        railhmi_test::check(r.retained_bytes == 10, "capacity: retained 10");
    }
}

void test_gap_merge() {
    std::vector<RecordingGapRow> gaps;
    add_gap(gaps, "CAM-01", 100, 200, "drop");
    add_gap(gaps, "CAM-01", 200, 300, "drop");   // 相邻 -> 合并
    add_gap(gaps, "CAM-01", 150, 160, "x");      // 重叠 -> 合并
    railhmi_test::check(gaps.size() == 1, "gap merge: adjacent/overlap merged to one");
    if (!gaps.empty()) {
        railhmi_test::check(gaps[0].t0_ms == 100 && gaps[0].t1_ms == 300, "gap merge bounds 100..300");
    }
    add_gap(gaps, "CAM-02", 100, 200, "y");      // 不同相机 -> 新缺口
    railhmi_test::check(gaps.size() == 2, "gap merge: different camera separate");
}

#ifdef RAILHMI_HAVE_SQLITE

bool copy_file(const std::string& src, const std::string& dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << in.rdbuf();
    return static_cast<bool>(out);
}

void remove_db(const std::string& path) {
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
    std::remove((path + ".copy").c_str());
    std::remove((path + ".copy-wal").c_str());
    std::remove((path + ".copy-shm").c_str());
}

void test_sqlite_repository() {
    using namespace railhmi::platform;
    const std::string path = "/tmp/railhmi_item007.db";
    remove_db(path);

    long long eid = -1, s1 = -1, s2 = -1;
    {
        SqliteRepository repo;
        std::string err;
        railhmi_test::check(repo.open(path, &err), "repo.open", err);
        railhmi_test::check(repo.init_schema(&err), "repo.init_schema", err);
        const std::string jm = repo.journal_mode(&err);
        railhmi_test::check(jm == "wal", "repo journal_mode == wal", jm);

        EventRow e;
        e.level = 1;
        e.type = "person_on_rail";
        e.source_cam = "CAM-01";
        e.ai_profile = "front";
        e.ts_wall_ms = 1000;
        e.ts_mono_us = 5000000;
        e.confidence = 0.9;
        e.distance_m = 8.0;
        e.algo = "yolov8s-seg";
        e.model_version = "2.3.2";
        e.description = "desc";
        eid = repo.insert_event(e, &err);
        railhmi_test::check(eid > 0, "insert_event returns id", err);

        EventTargetRow t; t.event_id = eid; t.track_id = 7; t.boxes_json = "[]"; t.first_ts = 1; t.last_ts = 2; t.duration_ms = 1;
        railhmi_test::check(repo.insert_event_target(t, &err), "insert_event_target", err);

        MaintenanceLogRow m; m.ts_wall_ms = 1000; m.ts_mono_us = 5000000; m.user = "admin"; m.action = "homing"; m.target = "CAM-01"; m.result = "ok";
        railhmi_test::check(repo.insert_maintenance(m, &err), "insert_maintenance", err);

        DeviceHealthRow h; h.ts_wall_ms = 1000; h.ts_mono_us = 5000000; h.npu_load = 12.5; h.cpu = 30.0; h.temp_c = 45.0; h.net_errors = 0; h.disk_free = 100; h.disk_total = 200;
        railhmi_test::check(repo.insert_health(h, &err), "insert_health", err);

        StreamEventRow se; se.ts_wall_ms = 1000; se.ts_mono_us = 5000000; se.cam = "CAM-01"; se.event = "reconnect"; se.detail = "attempt 1";
        railhmi_test::check(repo.insert_stream_event(se, &err) > 0, "insert_stream_event", err);

        IntegrationRow in; in.id = "NVR"; in.name = "nvr"; in.address = "192.168.1.4"; in.registered = true; in.state = "unconfigured";
        railhmi_test::check(repo.upsert_integration(in, &err), "upsert_integration", err);

        ConfigKvRow c; c.key = "render_path"; c.value = "cpu"; c.updated_ts = 1000; c.updated_by = "admin";
        railhmi_test::check(repo.set_config(c, &err), "set_config", err);
        railhmi_test::check(repo.get_config("render_path", &err) == "cpu", "get_config round-trip");

        RecordingSegmentRow rs1; rs1.cam = "CAM-01"; rs1.t0_ms = 0; rs1.t1_ms = 30000; rs1.path = "a.ts"; rs1.kind = SegmentKind::Continuous; rs1.complete = true; rs1.size_bytes = 10;
        RecordingSegmentRow rs2; rs2.cam = "CAM-01"; rs2.t0_ms = 30000; rs2.t1_ms = 90000; rs2.path = "b.ts"; rs2.kind = SegmentKind::Critical; rs2.complete = true; rs2.size_bytes = 10;
        s1 = repo.insert_segment(rs1, &err);
        s2 = repo.insert_segment(rs2, &err);
        railhmi_test::check(s1 > 0 && s2 > 0, "insert_segment x2", err);

        RecordingGapRow g; g.cam = "CAM-01"; g.t0_ms = 50000; g.t1_ms = 60000; g.reason = "drop";
        railhmi_test::check(repo.insert_gap(g, &err) > 0, "insert_gap", err);

        // 9 张表均有 1 行（recording_segments 有 2 行）
        const char* tables[] = {"events", "event_targets", "maintenance_log", "device_health",
                                "recording_segments", "recording_gaps", "stream_events",
                                "integrations", "config_kv"};
        long long counts[9];
        for (int i = 0; i < 9; ++i) counts[i] = repo.count(tables[i], &err);
        railhmi_test::check(counts[0] == 1 && counts[1] == 1 && counts[2] == 1 && counts[3] == 1 &&
                                counts[4] == 2 && counts[5] == 1 && counts[6] == 1 && counts[7] == 1 && counts[8] == 1,
                            "9 tables populated (segments=2)");

        // WAL 重放：连接仍打开时复制 db + -wal，打开副本应能看到已提交数据
        const bool copied = copy_file(path, path + ".copy");
        copy_file(path + "-wal", path + ".copy-wal");
        railhmi_test::check(copied, "copy db for WAL replay");
        {
            SqliteRepository cp;
            std::string e2;
            railhmi_test::check(cp.open(path + ".copy", &e2), "open WAL copy", e2);
            railhmi_test::check(cp.count("events", &e2) == 1, "WAL replay: event recovered from -wal");
            railhmi_test::check(cp.count("recording_gaps", &e2) == 1, "WAL replay: gap recovered");
            cp.close();
        }
        repo.close();
    }

    {   // 写入-重启-读取一致
        SqliteRepository repo;
        std::string err;
        railhmi_test::check(repo.open(path, &err), "reopen after restart", err);
        bool found = false;
        EventRow got = repo.get_event(eid, &found, &err);
        railhmi_test::check(found && got.id == eid && got.type == "person_on_rail" &&
                                got.ts_wall_ms == 1000 && got.ts_mono_us == 5000000 &&
                                got.confidence == 0.9,
                            "write-restart-read consistent");
        railhmi_test::check(repo.count("recording_gaps", &err) == 1, "gap persisted across restart");
        auto gaps = repo.query_gaps("CAM-01", &err);
        railhmi_test::check(gaps.size() == 1 && gaps[0].t0_ms == 50000 && gaps[0].reason == "drop",
                            "gaps queryable by camera");
        auto segs = repo.query_segments(&err);
        railhmi_test::check(segs.size() == 2 && segs[0].kind == SegmentKind::Continuous &&
                                segs[1].kind == SegmentKind::Critical,
                            "segments read back with kind");

        // 保留策略：容量 10 字节 -> 淘汰普通片段(s1)，保留保护片段(s2)
        RetentionPolicy p;
        p.retention_days = 1000;
        p.max_gb = bytes_to_gb(10);
        RetentionResult r = repo.apply_retention(p, 100000000LL, &err);
        railhmi_test::check(r.evicted_ids.size() == 1 && r.evicted_ids[0] == s1,
                            "repo retention evicts continuous first");
        railhmi_test::check(repo.count("recording_segments", &err) == 1, "repo retention: one segment left");
        auto left = repo.query_segments(&err);
        railhmi_test::check(left.size() == 1 && left[0].kind == SegmentKind::Critical,
                            "repo retention keeps critical segment");
        repo.close();
    }
    remove_db(path);
}

#endif  // RAILHMI_HAVE_SQLITE

}  // namespace

void run_persistence_tests() {
    test_timebase();
    test_retention_age();
    test_retention_capacity();
    test_gap_merge();
#ifdef RAILHMI_HAVE_SQLITE
    test_sqlite_repository();
#else
    std::printf("[SKIP] sqlite repository tests (RAILHMI_HAVE_SQLITE not defined)\n");
#endif
}
