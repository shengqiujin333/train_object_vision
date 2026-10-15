#include "railhmi/core/persistence_model.h"

namespace railhmi {
namespace core {

const char* to_string(SegmentKind k) {
    return k == SegmentKind::Critical ? "critical" : "continuous";
}

bool parse_segment_kind(const std::string& s, SegmentKind* out) {
    if (s == "continuous") { *out = SegmentKind::Continuous; return true; }
    if (s == "critical") { *out = SegmentKind::Critical; return true; }
    return false;
}

const char* schema_ddl() {
    static const char* ddl =
        "CREATE TABLE IF NOT EXISTS events ("
        " id INTEGER PRIMARY KEY, level INTEGER, type TEXT, source_cam TEXT, ai_profile TEXT,"
        " ts_wall_ms INTEGER, ts_mono_us INTEGER, confidence REAL, distance_m REAL,"
        " algo TEXT, model_version TEXT, description TEXT, ack_state TEXT,"
        " record_available INTEGER, snapshot_ref TEXT);"
        "CREATE TABLE IF NOT EXISTS event_targets ("
        " event_id INTEGER, track_id INTEGER, boxes_json TEXT,"
        " first_ts INTEGER, last_ts INTEGER, duration_ms INTEGER);"
        "CREATE TABLE IF NOT EXISTS maintenance_log ("
        " ts_wall_ms INTEGER, ts_mono_us INTEGER, user TEXT, action TEXT,"
        " target TEXT, result TEXT, detail TEXT);"
        "CREATE TABLE IF NOT EXISTS device_health ("
        " ts_wall_ms INTEGER, ts_mono_us INTEGER, npu_load REAL, cpu REAL, temp_c REAL,"
        " net_errors INTEGER, disk_free INTEGER, disk_total INTEGER);"
        "CREATE TABLE IF NOT EXISTS recording_segments ("
        " id INTEGER PRIMARY KEY, cam TEXT, t0_ms INTEGER, t1_ms INTEGER, path TEXT,"
        " kind TEXT, complete INTEGER, size_bytes INTEGER);"
        "CREATE TABLE IF NOT EXISTS recording_gaps ("
        " id INTEGER PRIMARY KEY, cam TEXT, t0_ms INTEGER, t1_ms INTEGER, reason TEXT);"
        "CREATE TABLE IF NOT EXISTS stream_events ("
        " id INTEGER PRIMARY KEY, ts_wall_ms INTEGER, ts_mono_us INTEGER, cam TEXT,"
        " event TEXT, detail TEXT);"
        "CREATE TABLE IF NOT EXISTS integrations ("
        " id TEXT PRIMARY KEY, name TEXT, address TEXT, registered INTEGER, state TEXT,"
        " protocol TEXT, channels_json TEXT, last_probe_ts INTEGER);"
        "CREATE TABLE IF NOT EXISTS config_kv ("
        " key TEXT PRIMARY KEY, value TEXT, updated_ts INTEGER, updated_by TEXT);";
    return ddl;
}

}  // namespace core
}  // namespace railhmi
