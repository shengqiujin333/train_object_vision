#pragma once

// railhmi-core: 持久化数据模型与 schema（ITEM-007）。
// 纯数据 + DDL 字符串；不含 SQLite/Qt 依赖。具体执行在 railhmi-platform（SqliteRepository）。
// 时间基准：统一 UTC 毫秒（ts_wall_ms）+ 单调微秒（ts_mono_us）成对存储。

#include <string>
#include <vector>

namespace railhmi {
namespace core {

enum class SegmentKind { Continuous, Critical };
const char* to_string(SegmentKind k);
bool parse_segment_kind(const std::string& s, SegmentKind* out);

struct EventRow {
    long long id = -1;                 // -1 -> 由数据库分配
    int level = 1;                     // 1|2|3
    std::string type;
    std::string source_cam;
    std::string ai_profile;
    long long ts_wall_ms = 0;
    long long ts_mono_us = 0;
    double confidence = 0.0;
    double distance_m = -1.0;
    std::string algo;
    std::string model_version;
    std::string description;
    std::string ack_state = "unacked";
    bool record_available = false;
    std::string snapshot_ref;
};

struct EventTargetRow {
    long long event_id = 0;
    long long track_id = 0;
    std::string boxes_json;
    long long first_ts = 0;
    long long last_ts = 0;
    long long duration_ms = 0;
};

struct MaintenanceLogRow {
    long long ts_wall_ms = 0;
    long long ts_mono_us = 0;
    std::string user;
    std::string action;
    std::string target;
    std::string result;
    std::string detail;
};

struct DeviceHealthRow {
    long long ts_wall_ms = 0;
    long long ts_mono_us = 0;
    double npu_load = -1.0;
    double cpu = -1.0;
    double temp_c = -1.0;
    long long net_errors = 0;
    long long disk_free = 0;
    long long disk_total = 0;
};

struct RecordingSegmentRow {
    long long id = -1;
    std::string cam;
    long long t0_ms = 0;
    long long t1_ms = 0;
    std::string path;
    SegmentKind kind = SegmentKind::Continuous;
    bool complete = false;
    long long size_bytes = 0;
};

struct RecordingGapRow {
    long long id = -1;
    std::string cam;
    long long t0_ms = 0;
    long long t1_ms = 0;
    std::string reason;
};

struct StreamEventRow {
    long long id = -1;
    long long ts_wall_ms = 0;
    long long ts_mono_us = 0;
    std::string cam;
    std::string event;
    std::string detail;
};

struct IntegrationRow {
    std::string id;
    std::string name;
    std::string address;
    bool registered = false;
    std::string state;          // probing/reachable/unreachable/unconfigured/unverified
    std::string protocol;
    std::string channels_json;
    long long last_probe_ts = 0;
};

struct ConfigKvRow {
    std::string key;
    std::string value;
    long long updated_ts = 0;
    std::string updated_by;
};

// 9 张表的 CREATE TABLE IF NOT EXISTS 语句（events/event_targets/maintenance_log/
// device_health/recording_segments/recording_gaps/stream_events/integrations/config_kv）。
const char* schema_ddl();

}  // namespace core
}  // namespace railhmi
