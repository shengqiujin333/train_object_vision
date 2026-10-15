#include "railhmi/platform/repository.h"

#ifdef RAILHMI_HAVE_SQLITE

#include "sqlite3_min.h"

#include <algorithm>

namespace railhmi {
namespace platform {

namespace {

struct Val {
    enum Kind { Null, Int, Real, Text } kind;
    long long i = 0;
    double d = 0.0;
    std::string s;
};
Val v_int(long long x) { Val v; v.kind = Val::Int; v.i = x; return v; }
Val v_real(double x) { Val v; v.kind = Val::Real; v.d = x; return v; }
Val v_text(std::string x) { Val v; v.kind = Val::Text; v.s = std::move(x); return v; }
Val v_null() { Val v; v.kind = Val::Null; return v; }

long long insert_row(sqlite3* db, const std::string& table,
                     const std::vector<std::string>& cols, const std::vector<Val>& vals,
                     std::string* err) {
    if (cols.size() != vals.size()) { if (err) *err = "insert: column/value size mismatch"; return -1; }
    std::string sql = "INSERT INTO " + table + "(";
    for (std::size_t i = 0; i < cols.size(); ++i) { if (i) sql += ","; sql += cols[i]; }
    sql += ") VALUES(";
    for (std::size_t i = 0; i < cols.size(); ++i) { if (i) sql += ","; sql += "?"; }
    sql += ");";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = std::string("prepare insert failed: ") + sqlite3_errmsg(db);
        return -1;
    }
    for (std::size_t i = 0; i < vals.size(); ++i) {
        const int idx = static_cast<int>(i) + 1;
        switch (vals[i].kind) {
            case Val::Int: sqlite3_bind_int64(st, idx, vals[i].i); break;
            case Val::Real: sqlite3_bind_double(st, idx, vals[i].d); break;
            case Val::Text: sqlite3_bind_text(st, idx, vals[i].s.c_str(), -1, SQLITE_TRANSIENT); break;
            case Val::Null: sqlite3_bind_null(st, idx); break;
        }
    }
    long long id = -1;
    if (sqlite3_step(st) == SQLITE_DONE) {
        id = sqlite3_last_insert_rowid(db);
    } else if (err) {
        *err = std::string("insert step failed: ") + sqlite3_errmsg(db);
    }
    sqlite3_finalize(st);
    return id;
}

std::string col_text(sqlite3_stmt* st, int i) {
    const unsigned char* t = sqlite3_column_text(st, i);
    return t ? reinterpret_cast<const char*>(t) : std::string();
}

}  // namespace

bool SqliteRepository::open(const std::string& path, std::string* err) {
    return store_.open(path, err);
}
void SqliteRepository::close() { store_.close(); }

bool SqliteRepository::init_schema(std::string* err) {
    return store_.exec(core::schema_ddl(), err);
}

std::string SqliteRepository::journal_mode(std::string* err) {
    return store_.journal_mode(err);
}

long long SqliteRepository::insert_event(const core::EventRow& e, std::string* err) {
    return insert_row(store_.handle(), "events",
        {"id", "level", "type", "source_cam", "ai_profile", "ts_wall_ms", "ts_mono_us",
         "confidence", "distance_m", "algo", "model_version", "description", "ack_state",
         "record_available", "snapshot_ref"},
        {e.id >= 0 ? v_int(e.id) : v_null(), v_int(e.level), v_text(e.type), v_text(e.source_cam),
         v_text(e.ai_profile), v_int(e.ts_wall_ms), v_int(e.ts_mono_us), v_real(e.confidence),
         v_real(e.distance_m), v_text(e.algo), v_text(e.model_version), v_text(e.description),
         v_text(e.ack_state), v_int(e.record_available ? 1 : 0), v_text(e.snapshot_ref)},
        err);
}

bool SqliteRepository::insert_event_target(const core::EventTargetRow& t, std::string* err) {
    return insert_row(store_.handle(), "event_targets",
        {"event_id", "track_id", "boxes_json", "first_ts", "last_ts", "duration_ms"},
        {v_int(t.event_id), v_int(t.track_id), v_text(t.boxes_json), v_int(t.first_ts),
         v_int(t.last_ts), v_int(t.duration_ms)}, err) >= 0;
}

bool SqliteRepository::insert_maintenance(const core::MaintenanceLogRow& m, std::string* err) {
    return insert_row(store_.handle(), "maintenance_log",
        {"ts_wall_ms", "ts_mono_us", "user", "action", "target", "result", "detail"},
        {v_int(m.ts_wall_ms), v_int(m.ts_mono_us), v_text(m.user), v_text(m.action),
         v_text(m.target), v_text(m.result), v_text(m.detail)}, err) >= 0;
}

bool SqliteRepository::insert_health(const core::DeviceHealthRow& h, std::string* err) {
    return insert_row(store_.handle(), "device_health",
        {"ts_wall_ms", "ts_mono_us", "npu_load", "cpu", "temp_c", "net_errors", "disk_free", "disk_total"},
        {v_int(h.ts_wall_ms), v_int(h.ts_mono_us), v_real(h.npu_load), v_real(h.cpu), v_real(h.temp_c),
         v_int(h.net_errors), v_int(h.disk_free), v_int(h.disk_total)}, err) >= 0;
}

long long SqliteRepository::insert_segment(const core::RecordingSegmentRow& s, std::string* err) {
    return insert_row(store_.handle(), "recording_segments",
        {"id", "cam", "t0_ms", "t1_ms", "path", "kind", "complete", "size_bytes"},
        {s.id >= 0 ? v_int(s.id) : v_null(), v_text(s.cam), v_int(s.t0_ms), v_int(s.t1_ms),
         v_text(s.path), v_text(core::to_string(s.kind)), v_int(s.complete ? 1 : 0),
         v_int(s.size_bytes)}, err);
}

long long SqliteRepository::insert_gap(const core::RecordingGapRow& g, std::string* err) {
    return insert_row(store_.handle(), "recording_gaps",
        {"cam", "t0_ms", "t1_ms", "reason"},
        {v_text(g.cam), v_int(g.t0_ms), v_int(g.t1_ms), v_text(g.reason)}, err);
}

long long SqliteRepository::insert_stream_event(const core::StreamEventRow& s, std::string* err) {
    return insert_row(store_.handle(), "stream_events",
        {"ts_wall_ms", "ts_mono_us", "cam", "event", "detail"},
        {v_int(s.ts_wall_ms), v_int(s.ts_mono_us), v_text(s.cam), v_text(s.event), v_text(s.detail)}, err);
}

bool SqliteRepository::upsert_integration(const core::IntegrationRow& i, std::string* err) {
    const std::string sql =
        "INSERT OR REPLACE INTO integrations(id,name,address,registered,state,protocol,channels_json,last_probe_ts)"
        " VALUES(?,?,?,?,?,?,?,?);";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(store_.handle(), sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(store_.handle());
        return false;
    }
    sqlite3_bind_text(st, 1, i.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, i.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, i.address.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, i.registered ? 1 : 0);
    sqlite3_bind_text(st, 5, i.state.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, i.protocol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, i.channels_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 8, i.last_probe_ts);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    if (!ok && err) *err = sqlite3_errmsg(store_.handle());
    sqlite3_finalize(st);
    return ok;
}

bool SqliteRepository::set_config(const core::ConfigKvRow& c, std::string* err) {
    const std::string sql =
        "INSERT OR REPLACE INTO config_kv(key,value,updated_ts,updated_by) VALUES(?,?,?,?);";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(store_.handle(), sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(store_.handle());
        return false;
    }
    sqlite3_bind_text(st, 1, c.key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, c.value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, c.updated_ts);
    sqlite3_bind_text(st, 4, c.updated_by.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    if (!ok && err) *err = sqlite3_errmsg(store_.handle());
    sqlite3_finalize(st);
    return ok;
}

std::string SqliteRepository::get_config(const std::string& key, std::string* err) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(store_.handle(), "SELECT value FROM config_kv WHERE key=?;", -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(store_.handle());
        return std::string();
    }
    sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    std::string value;
    if (sqlite3_step(st) == SQLITE_ROW) value = col_text(st, 0);
    sqlite3_finalize(st);
    return value;
}

long long SqliteRepository::count(const std::string& table, std::string* err) {
    long long n = -1;
    store_.query_int("SELECT count(*) FROM " + table + ";", &n, err);
    return n;
}

core::EventRow SqliteRepository::get_event(long long id, bool* found, std::string* err) {
    core::EventRow e;
    if (found) *found = false;
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "SELECT id,level,type,source_cam,ai_profile,ts_wall_ms,ts_mono_us,confidence,distance_m,"
        "algo,model_version,description,ack_state,record_available,snapshot_ref FROM events WHERE id=?;";
    if (sqlite3_prepare_v2(store_.handle(), sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(store_.handle());
        return e;
    }
    sqlite3_bind_int64(st, 1, id);
    if (sqlite3_step(st) == SQLITE_ROW) {
        if (found) *found = true;
        e.id = sqlite3_column_int64(st, 0);
        e.level = static_cast<int>(sqlite3_column_int64(st, 1));
        e.type = col_text(st, 2);
        e.source_cam = col_text(st, 3);
        e.ai_profile = col_text(st, 4);
        e.ts_wall_ms = sqlite3_column_int64(st, 5);
        e.ts_mono_us = sqlite3_column_int64(st, 6);
        e.confidence = sqlite3_column_double(st, 7);
        e.distance_m = sqlite3_column_double(st, 8);
        e.algo = col_text(st, 9);
        e.model_version = col_text(st, 10);
        e.description = col_text(st, 11);
        e.ack_state = col_text(st, 12);
        e.record_available = sqlite3_column_int64(st, 13) != 0;
        e.snapshot_ref = col_text(st, 14);
    }
    sqlite3_finalize(st);
    return e;
}

std::vector<core::RecordingSegmentRow> SqliteRepository::query_segments(std::string* err) {
    std::vector<core::RecordingSegmentRow> out;
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "SELECT id,cam,t0_ms,t1_ms,path,kind,complete,size_bytes FROM recording_segments ORDER BY t0_ms,id;";
    if (sqlite3_prepare_v2(store_.handle(), sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(store_.handle());
        return out;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        core::RecordingSegmentRow s;
        s.id = sqlite3_column_int64(st, 0);
        s.cam = col_text(st, 1);
        s.t0_ms = sqlite3_column_int64(st, 2);
        s.t1_ms = sqlite3_column_int64(st, 3);
        s.path = col_text(st, 4);
        core::parse_segment_kind(col_text(st, 5), &s.kind);
        s.complete = sqlite3_column_int64(st, 6) != 0;
        s.size_bytes = sqlite3_column_int64(st, 7);
        out.push_back(std::move(s));
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<core::RecordingGapRow> SqliteRepository::query_gaps(const std::string& cam, std::string* err) {
    std::vector<core::RecordingGapRow> out;
    std::string sql = "SELECT id,cam,t0_ms,t1_ms,reason FROM recording_gaps";
    if (!cam.empty()) sql += " WHERE cam=?";
    sql += " ORDER BY t0_ms,id;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(store_.handle(), sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(store_.handle());
        return out;
    }
    if (!cam.empty()) sqlite3_bind_text(st, 1, cam.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        core::RecordingGapRow g;
        g.id = sqlite3_column_int64(st, 0);
        g.cam = col_text(st, 1);
        g.t0_ms = sqlite3_column_int64(st, 2);
        g.t1_ms = sqlite3_column_int64(st, 3);
        g.reason = col_text(st, 4);
        out.push_back(std::move(g));
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<core::StreamEventRow> SqliteRepository::query_stream_events(std::string* err) {
    std::vector<core::StreamEventRow> out;
    sqlite3_stmt* st = nullptr;
    const char* sql = "SELECT id,ts_wall_ms,ts_mono_us,cam,event,detail FROM stream_events ORDER BY id;";
    if (sqlite3_prepare_v2(store_.handle(), sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(store_.handle());
        return out;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        core::StreamEventRow e;
        e.id = sqlite3_column_int64(st, 0);
        e.ts_wall_ms = sqlite3_column_int64(st, 1);
        e.ts_mono_us = sqlite3_column_int64(st, 2);
        e.cam = col_text(st, 3);
        e.event = col_text(st, 4);
        e.detail = col_text(st, 5);
        out.push_back(std::move(e));
    }
    sqlite3_finalize(st);
    return out;
}

core::RetentionResult SqliteRepository::apply_retention(const core::RetentionPolicy& policy,
                                                        long long now_ms, std::string* err) {
    std::vector<core::RecordingSegmentRow> segs = query_segments(err);
    core::RetentionResult r = core::apply_retention(segs, now_ms, policy);
    for (long long id : r.evicted_ids) {
        store_.exec("DELETE FROM recording_segments WHERE id=" + std::to_string(id) + ";", err);
    }
    return r;
}

}  // namespace platform
}  // namespace railhmi

#endif  // RAILHMI_HAVE_SQLITE
