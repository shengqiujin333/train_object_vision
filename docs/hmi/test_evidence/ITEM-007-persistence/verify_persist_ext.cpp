// software_verification extended fixtures for ITEM-007 persistence & retention (verifier-owned).
// Compiled standalone on the RK3588 board against railhmi core/platform sources and the
// runtime libsqlite3.so.0. Focus beyond the implementer's first-party cover:
//   1. SIGKILL mid-write semantics (real process kill, not copy-based replay): committed rows
//      survive, uncommitted transaction row invisible, journal mode stays WAL (TC-CORE-06).
//   2. retention boundary/ties: exact age limit, exact capacity, continuous-vs-critical tie at
//      equal t0, retention_days=0, empty input, incomplete segment aging.
//   3. gap model: containment, 1ms-hole separation, bridging merge, reversed input, ordering,
//      repository query with/without camera filter.
//   4. schema DDL conformity to DESKTOP_APP_DESIGN §11.1: 9 tables, exact column counts,
//      paired ts_wall_ms/ts_mono_us on event-class tables, kind enum round-trip.
//   5. repository upsert overwrite semantics, explicit event id, duplicate-PK failure reporting,
//      live age-policy retention execution against the DB.
#include "railhmi/core/persistence_model.h"
#include "railhmi/core/retention.h"
#include "railhmi/core/timebase.h"
#include "railhmi/platform/repository.h"
#include "sqlite3_min.h"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace railhmi;
using namespace railhmi::core;
using railhmi::platform::SqliteRepository;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg)                                       \
    do {                                                       \
        if (cond) { ++g_pass; std::printf("[PASS] %s\n", msg); }       \
        else { ++g_fail; std::printf("[FAIL] %s\n", msg); }            \
    } while (0)

namespace {

double gb(long long bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0); }

void drop_db(const std::string& path) {
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}

int raw_qi(sqlite3* db, const char* sql) {  // single integer quantity query
    sqlite3_stmt* st = nullptr;
    int n = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
        n = static_cast<int>(sqlite3_column_int64(st, 0));
    sqlite3_finalize(st);
    return n;
}

void sqlexec(sqlite3* db, const char* sql) {
    char* msg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &msg) != SQLITE_OK && msg)
        std::printf("[INFO] exec failed: %s (%s)\n", sql, msg);
    if (msg) sqlite3_free(msg);
}

EventRow event_row(const char* type, long long id = -1) {
    EventRow e;
    e.id = id;
    e.level = 1;
    e.type = type;
    e.source_cam = "CAM-01";
    e.ai_profile = "front";
    e.ts_wall_ms = 1000;
    e.ts_mono_us = 1000000;
    e.confidence = 0.9;
    e.distance_m = -1.0;
    e.algo = "yolov8s-seg";
    e.model_version = "2.3.2";
    e.description = "fx";
    e.ack_state = "unacked";
    e.record_available = false;
    e.snapshot_ref = "";
    return e;
}

RecordingSegmentRow seg(long long id, SegmentKind kind, long long t0, long long t1, long long bytes) {
    RecordingSegmentRow s;
    s.id = id;
    s.kind = kind;
    s.t0_ms = t0;
    s.t1_ms = t1;
    s.size_bytes = bytes;
    return s;
}

// ---------------- 4. schema DDL conformity ----------------
void test_ddl_conformity() {
    const std::string path = "/tmp/railhmi_v7fx_ddl.db";
    drop_db(path);
    sqlite3* db = nullptr;
    CHECK(sqlite3_open(path.c_str(), &db) == SQLITE_OK && db, "fx ddl: open temp db");
    if (!db) return;
    sqlexec(db, core::schema_ddl());
    const char* tables[9] = {"events", "event_targets", "maintenance_log", "device_health",
                             "recording_segments", "recording_gaps", "stream_events",
                             "integrations", "config_kv"};
    const int cols[9] = {15, 6, 7, 8, 8, 5, 6, 8, 4};
    bool all_ok = true;
    for (int i = 0; i < 9; ++i) {
        char q[256];
        std::snprintf(q, sizeof(q),
            "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='%s';", tables[i]);
        const int exists = raw_qi(db, q);
        std::snprintf(q, sizeof(q), "SELECT count(*) FROM pragma_table_info('%s');", tables[i]);
        const int ncols = raw_qi(db, q);
        if (exists != 1 || ncols != cols[i]) {
            all_ok = false;
            std::printf("[INFO] %s exists=%d cols=%d expected=%d\n", tables[i], exists, ncols, cols[i]);
        }
    }
    CHECK(all_ok, "fx ddl: 9 tables with design §11.1 column counts");
    bool pairs_ok = true;
    const char* paired[4] = {"events", "maintenance_log", "device_health", "stream_events"};
    for (int i = 0; i < 4; ++i) {
        char q[300];
        std::snprintf(q, sizeof(q),
            "SELECT (SELECT count(*) FROM pragma_table_info('%s') WHERE name='ts_wall_ms')"
            " * (SELECT count(*) FROM pragma_table_info('%s') WHERE name='ts_mono_us');",
            paired[i], paired[i]);
        if (raw_qi(db, q) != 1) pairs_ok = false;
    }
    CHECK(pairs_ok, "fx ddl: event-class tables pair ts_wall_ms + ts_mono_us");
    // design events row needs id PK; check the DDL declared it
    CHECK(raw_qi(db, "SELECT count(*) FROM pragma_table_info('events')")
              > 0 && raw_qi(db,
              "SELECT count(*) FROM sqlite_master WHERE name='events' AND sql LIKE '%PRIMARY KEY%';") == 1,
          "fx ddl: events/id uses INTEGER PRIMARY KEY");
    // kind text round-trip through parse
    SegmentKind kind = SegmentKind::Continuous;
    CHECK(std::string(to_string(SegmentKind::Critical)) == "critical"
              && parse_segment_kind("critical", &kind) && kind == SegmentKind::Critical
              && parse_segment_kind("continuous", &kind) && kind == SegmentKind::Continuous
              && !parse_segment_kind("unknown", &kind),
          "fx ddl: segment kind text round-trip + invalid literal rejected");
    sqlite3_close(db);
    drop_db(path);
}

// ---------------- 1. SIGKILL mid-write / WAL recovery ----------------
[[noreturn]] void run_crash_child(const std::string& path, bool commit_first) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) _exit(30);
    sqlexec(db, "PRAGMA journal_mode=WAL;");
    sqlexec(db, "BEGIN");
    sqlexec(db,
        "INSERT INTO events(level,type,source_cam,ai_profile,ts_wall_ms,ts_mono_us,confidence,"
        "distance_m,algo,model_version,description,ack_state,record_available,snapshot_ref) "
        "VALUES(1,'child_row','CAM-09','front',77,77000,0.5,-1,'t','t','x','unacked',0,'');");
    if (commit_first) sqlexec(db, "COMMIT");
    ::raise(SIGKILL);  // C raise(): no close, no checkpoint, no further writes
    _exit(31);         // unreachable
}

void test_crash_recovery() {
    const std::string path = "/tmp/railhmi_v7fx_crash.db";
    drop_db(path);

    // Parent: schema + exactly one committed row, handle closed before forking.
    sqlite3* db = nullptr;
    bool ready = sqlite3_open(path.c_str(), &db) == SQLITE_OK;
    if (ready) {
        sqlexec(db, core::schema_ddl());
        sqlexec(db, "PRAGMA journal_mode=WAL;");
        sqlexec(db,
            "INSERT INTO events(level,type,source_cam,ai_profile,ts_wall_ms,ts_mono_us,confidence,"
            "distance_m,algo,model_version,description,ack_state,record_available,snapshot_ref) "
            "VALUES(1,'parent_row','CAM-01','front',1,1000,0.9,-1,'a','a','p','unacked',0,'');");
        sqlite3_close(db);
    }
    CHECK(ready, "fx crash: parent prepared schema + committed row");

    // (a) uncommitted transaction killed mid-write -> must stay invisible
    pid_t p1 = fork();
    if (p1 == 0) run_crash_child(path, false);  // never returns
    int st1 = 0;
    waitpid(p1, &st1, 0);
    CHECK(WIFSIGNALED(st1) && WTERMSIG(st1) == SIGKILL,
          "fx crash: child killed by SIGKILL mid-write (uncommitted)");

    bool opened = false, invis = false, wal_ok = false;
    {
        sqlite3* r = nullptr;
        if (sqlite3_open(path.c_str(), &r) == SQLITE_OK) {
            opened = true;
            invis = raw_qi(r, "SELECT count(*) FROM events;") == 1
                && raw_qi(r, "SELECT count(*) FROM events WHERE type='child_row';") == 0
                && raw_qi(r, "SELECT count(*) FROM events WHERE type='parent_row';") == 1;
            char jm[64] = {0};
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(r, "PRAGMA journal_mode;", -1, &st, nullptr) == SQLITE_OK
                && sqlite3_step(st) == SQLITE_ROW) {
                const unsigned char* t = sqlite3_column_text(st, 0);
                std::snprintf(jm, sizeof(jm), "%s", t ? reinterpret_cast<const char*>(t) : "?");
            }
            sqlite3_finalize(st);
            wal_ok = std::string(jm) == "wal";
            sqlite3_close(r);
        }
    }
    CHECK(opened, "fx crash: DB reopens after mid-write kill");
    CHECK(invis, "fx crash: uncommitted row invisible, committed row intact");
    CHECK(wal_ok, "fx crash: journal_mode still wal after recovery");

    // (b) committed then killed without close/checkpoint -> must survive (WAL replay)
    drop_db(path);
    sqlite3* db2 = nullptr;
    ready = sqlite3_open(path.c_str(), &db2) == SQLITE_OK;
    if (ready) {
        sqlexec(db2, core::schema_ddl());
        sqlexec(db2, "PRAGMA journal_mode=WAL;");
        sqlite3_close(db2);
    }
    CHECK(ready, "fx crash: fresh WAL db prepared");
    pid_t p2 = fork();
    if (p2 == 0) run_crash_child(path, true);  // commit then SIGKILL
    int st2 = 0;
    waitpid(p2, &st2, 0);
    CHECK(WIFSIGNALED(st2) && WTERMSIG(st2) == SIGKILL,
          "fx crash: child killed by SIGKILL after commit, no close");
    bool survived = false;
    {
        SqliteRepository repo;
        std::string err;
        if (repo.open(path, &err)) {
            survived = repo.count("events", &err) == 1 && repo.journal_mode(&err) == "wal";
            repo.close();
        } else {
            std::printf("[INFO] reopen via repo failed: %s\n", err.c_str());
        }
    }
    CHECK(survived, "fx crash: committed row recovered via SqliteRepository (WAL replay)");
    drop_db(path);
}

// ---------------- 2. retention boundary/tie edge cases ----------------
void test_retention_edges() {
    const long long day = 86400000LL;
    const long long now = 100LL * day;

    {   // exact age limit is NOT evicted (strict >), one ms past is
        std::vector<RecordingSegmentRow> segs;
        segs.push_back(seg(1, SegmentKind::Continuous, 0, now - 1 * day, 10));        // exactly limit
        segs.push_back(seg(2, SegmentKind::Continuous, 0, now - 1 * day - 1, 10));    // past limit
        segs.push_back(seg(3, SegmentKind::Critical, 0, now - 10 * day, 10));         // never by age
        RetentionPolicy p;
        p.retention_days = 1;
        p.max_gb = 1000.0;
        RetentionResult r = apply_retention(segs, now, p);
        CHECK(r.evicted_ids.size() == 1 && r.evicted_ids[0] == 2,
              "fx retention: age boundary strict — exact limit kept, +1ms evicted");
        CHECK(r.evicted_critical == 0, "fx retention: critical exempt from age limit");
    }
    {   // exact capacity: total == limit -> nothing evicted; slightly over -> oldest first
        std::vector<RecordingSegmentRow> segs;
        segs.push_back(seg(1, SegmentKind::Continuous, 0, 10, 10));
        segs.push_back(seg(2, SegmentKind::Continuous, 10, 20, 20));
        RetentionPolicy p;
        p.retention_days = 1000;
        p.max_gb = gb(30);
        RetentionResult r = apply_retention(segs, now, p);
        CHECK(r.evicted_ids.empty() && r.retained_bytes == 30,
              "fx retention: exactly at capacity evicts nothing");
        p.max_gb = gb(29);
        r = apply_retention(segs, now, p);
        CHECK(r.evicted_ids.size() == 1 && r.evicted_ids[0] == 1 && r.retained_bytes == 20,
              "fx retention: one byte over capacity evicts oldest only, stops under limit");
    }
    {   // tie at equal t0+t1: continuous before critical; then ascending id; pure-order probe
        std::vector<RecordingSegmentRow> segs;
        segs.push_back(seg(21, SegmentKind::Critical, 100, 200, 5));
        segs.push_back(seg(20, SegmentKind::Continuous, 100, 200, 5));
        segs.push_back(seg(22, SegmentKind::Critical, 100, 200, 5));
        RetentionPolicy p;
        p.retention_days = 1000;
        p.max_gb = 0.0;  // limit 0 -> evict everything; ordering matters
        RetentionResult r = apply_retention(segs, now, p);
        CHECK(r.evicted_ids.size() == 3 && r.evicted_ids[0] == 20 && r.evicted_ids[1] == 21
                  && r.evicted_ids[2] == 22,
              "fx retention: tie order continuous-first, then ascending id at equal t0");
        CHECK(r.evicted_critical == 2 && r.evicted_continuous == 1, "fx retention: tie eviction counts");
    }
    {   // retention_days=0: expired continuous goes, future-dated and critical stay
        std::vector<RecordingSegmentRow> segs;
        segs.push_back(seg(1, SegmentKind::Continuous, 0, now - 1, 10));
        segs.push_back(seg(2, SegmentKind::Continuous, 0, now + day, 10));  // future-dated
        segs.push_back(seg(3, SegmentKind::Critical, 0, now - 1, 10));
        RetentionPolicy p;
        p.retention_days = 0;
        p.max_gb = 1000.0;
        RetentionResult r = apply_retention(segs, now, p);
        CHECK(r.evicted_ids.size() == 1 && r.evicted_ids[0] == 1,
              "fx retention: days=0 evicts expired continuous only");
    }
    {   // empty input: no eviction, no crash
        std::vector<RecordingSegmentRow> empty;
        RetentionPolicy p;
        RetentionResult r = apply_retention(empty, now, p);
        CHECK(r.evicted_ids.empty() && r.retained_bytes == 0, "fx retention: empty input safe");
    }
    {   // incomplete continuous aging out is deterministic (kind decides, not complete flag)
        std::vector<RecordingSegmentRow> segs;
        RecordingSegmentRow s = seg(9, SegmentKind::Continuous, 0, now - 10 * day, 10);
        s.complete = false;
        segs.push_back(s);
        RetentionPolicy p;
        p.retention_days = 1;
        p.max_gb = 1000.0;
        RetentionResult r = apply_retention(segs, now, p);
        CHECK(r.evicted_ids.size() == 1 && r.evicted_ids[0] == 9 && r.evicted_continuous == 1,
              "fx retention: incomplete continuous ages out by same rule (observed behavior)");
    }
}

// ---------------- 3. gap model edges ----------------
void test_gap_edges() {
    {   // containment does not grow bounds; 1ms hole separates; later bridge — VERIFIER
        // EXPECTATION (declared contract "缺口按相机合并重叠/相邻区间，保持按 t0 排序"):
        // merging must cover ALL overlapping/adjacent same-camera intervals, not only the first.
        std::vector<RecordingGapRow> gaps;
        add_gap(gaps, "C1", 100, 300, "drop");
        add_gap(gaps, "C1", 120, 180, "x");        // fully contained
        CHECK(gaps.size() == 1 && gaps[0].t0_ms == 100 && gaps[0].t1_ms == 300,
              "fx gaps: contained interval leaves bounds unchanged");
        add_gap(gaps, "C1", 50, 99, "x");          // 1ms hole at 99..100
        CHECK(gaps.size() == 2 && gaps[0].t0_ms == 50 && gaps[0].t1_ms == 99,
              "fx gaps: 1ms hole keeps separate gap and sorted order");
        add_gap(gaps, "C1", 99, 110, "bridge");    // bridges both existing same-camera gaps
        CHECK(gaps.size() == 1 && gaps[0].t0_ms == 50 && gaps[0].t1_ms == 300,
              "fx gaps: bridging interval merges ALL overlapping pieces (declared contract)");
        CHECK(gaps[0].reason == "drop", "fx gaps: first existing non-empty reason kept on merge");
    }
    {   // reversed input normalized by swap
        std::vector<RecordingGapRow> gaps;
        add_gap(gaps, "C2", 300, 100, "r");
        CHECK(gaps.size() == 1 && gaps[0].t0_ms == 100 && gaps[0].t1_ms == 300,
              "fx gaps: reversed t0>t1 normalized");
    }
    {   // ordering by camera then t0
        std::vector<RecordingGapRow> gaps;
        add_gap(gaps, "B", 200, 210, "r");
        add_gap(gaps, "A", 10, 20, "r");
        add_gap(gaps, "B", 100, 110, "r");
        CHECK(gaps.size() == 3 && gaps[0].cam == "A" && gaps[1].cam == "B" &&
                  gaps[1].t0_ms == 100 && gaps[2].t0_ms == 200,
              "fx gaps: sorted by camera then t0");
    }
    {   // through the repository: two cameras, query all and by camera
        const std::string path = "/tmp/railhmi_v7fx_gap.db";
        drop_db(path);
        SqliteRepository repo;
        std::string err;
        const bool opened = repo.open(path, &err) && repo.init_schema(&err);
        CHECK(opened, "fx gaps: repo open+schema");
        RecordingGapRow g1;
        g1.cam = "CAM-A";
        g1.t0_ms = 900;
        g1.t1_ms = 950;
        g1.reason = "drop";
        RecordingGapRow g2;
        g2.cam = "CAM-B";
        g2.t0_ms = 10;
        g2.t1_ms = 20;
        g2.reason = "another";
        CHECK(repo.insert_gap(g1, &err) > 0 && repo.insert_gap(g2, &err) > 0,
              "fx gaps: insert two cameras");
        auto all = repo.query_gaps("", &err);
        CHECK(all.size() == 2 && all[0].cam == "CAM-B" && all[1].cam == "CAM-A",
              "fx gaps: empty filter returns all ordered by t0");
        auto only_a = repo.query_gaps("CAM-A", &err);
        CHECK(only_a.size() == 1 && only_a[0].reason == "drop", "fx gaps: camera filter isolates");
        repo.close();
        drop_db(path);
    }
}

// ---------------- 5. repository upsert / explicit id / live retention ----------------
void test_repo_semantics() {
    const std::string path = "/tmp/railhmi_v7fx_repo.db";
    drop_db(path);
    SqliteRepository repo;
    std::string err;
    CHECK(repo.open(path, &err), "fx repo: open");
    CHECK(repo.init_schema(&err), "fx repo: init_schema");

    ConfigKvRow c;
    c.key = "k";
    c.value = "v1";
    CHECK(repo.set_config(c, &err), "fx repo: set_config v1");
    c.value = "v2";
    CHECK(repo.set_config(c, &err), "fx repo: set_config v2");
    CHECK(repo.count("config_kv", &err) == 1 && repo.get_config("k", &err) == "v2",
          "fx repo: config_kv upsert overwrites (count==1, last value)");

    IntegrationRow in;
    in.id = "NVR";
    in.state = "probing";
    CHECK(repo.upsert_integration(in, &err), "fx repo: upsert probing");
    in.state = "unreachable";
    CHECK(repo.upsert_integration(in, &err), "fx repo: upsert unreachable");
    CHECK(repo.count("integrations", &err) == 1, "fx repo: integrations upsert count==1");
    bool found = false;
    // integrations row state readable via SQL through the raw handle is not needed —
    // use event route for explicit id checks instead:
    EventRow e = event_row("explicit_id", 777);
    CHECK(repo.insert_event(e, &err) == 777, "fx repo: explicit event id preserved");
    CHECK(repo.get_event(777, &found, &err).type == "explicit_id" && found,
          "fx repo: get_event finds explicit id");

    EventRow e2 = event_row("record_false", 778);
    CHECK(repo.insert_event(e2, &err) == 778, "fx repo: second explicit id");
    CHECK(!repo.get_event(778, &found, &err).record_available,
          "fx repo: record_available=false round-trips");

    EventRow dupe = event_row("dupe", 777);
    err = "";
    const long long dupe_rc = repo.insert_event(dupe, &err);
    CHECK(dupe_rc == -1 && !err.empty(), "fx repo: duplicate PK reported as failure");

    const long long day = 86400000LL;
    const long long now = 30LL * day;
    RecordingSegmentRow old_c;
    old_c.cam = "C";
    old_c.t0_ms = 0;
    old_c.t1_ms = now - 5 * day;
    old_c.kind = SegmentKind::Continuous;
    old_c.size_bytes = 10;
    RecordingSegmentRow old_k = old_c;
    old_k.kind = SegmentKind::Critical;
    RecordingSegmentRow fresh_c = old_c;
    fresh_c.t0_ms = now - 10;
    fresh_c.t1_ms = now - 5;
    const long long s1 = repo.insert_segment(old_c, &err);
    const long long s2 = repo.insert_segment(old_k, &err);
    const long long s3 = repo.insert_segment(fresh_c, &err);
    CHECK(s1 > 0 && s2 > 0 && s3 > 0, "fx repo: three segments inserted");
    RetentionPolicy p;
    p.retention_days = 1;
    p.max_gb = 1000.0;
    RetentionResult r = repo.apply_retention(p, now, &err);
    CHECK(r.evicted_ids.size() == 1 && r.evicted_ids[0] == s1 && r.evicted_continuous == 1,
          "fx repo: DB retention evicts expired continuous only");
    CHECK(repo.count("recording_segments", &err) == 2, "fx repo: DB retention leaves 2 segments");
    auto left = repo.query_segments(&err);
    CHECK(left.size() == 2, "fx repo: survivors queryable");
    bool critical_kept = false, fresh_kept = false;
    for (const auto& s : left) {
        if (s.id == s2 && s.kind == SegmentKind::Critical) critical_kept = true;
        if (s.id == s3 && s.t1_ms == now - 5) fresh_kept = true;
    }
    CHECK(critical_kept, "fx repo: critical survivor kept over age policy");
    CHECK(fresh_kept, "fx repo: fresh survivor kept over age policy");
    repo.close();
    drop_db(path);
}

}  // namespace

int main() {
    std::printf("== verify_item007 persistence/retention extension fixtures ==\n");
    test_ddl_conformity();
    test_retention_edges();
    test_gap_edges();
    test_repo_semantics();
    test_crash_recovery();
    std::printf("== %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
