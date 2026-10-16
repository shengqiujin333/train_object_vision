#include "railhmi/core/maintenance.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "railhmi/core/alarm_center.h"   // format_time_text

namespace railhmi {
namespace core {

const char* to_string(PtzResult r) {
    switch (r) {
        case PtzResult::Ok: return "ok";
        case PtzResult::OutOfTolerance: return "out_of_tolerance";
        case PtzResult::Unverified: return "unverified";
    }
    return "unverified";
}

const char* ptz_result_label(PtzResult r) {
    switch (r) {
        case PtzResult::Ok: return "容差内";
        case PtzResult::OutOfTolerance: return "超差";
        case PtzResult::Unverified: return "未核验";
    }
    return "未核验";
}

namespace {
std::string fmt2(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.2f", v);
    return b;
}
std::string fmt_bytes(long long bytes) {
    const double gib = 1024.0 * 1024.0 * 1024.0;
    char b[48];
    if (bytes >= static_cast<long long>(gib))
        std::snprintf(b, sizeof(b), "%.1f GB", bytes / gib);
    else
        std::snprintf(b, sizeof(b), "%.0f MB", bytes / (1024.0 * 1024.0));
    return b;
}
}  // namespace

PtzCheck ptz_check(const PtzState& s, const PtzTolerance& tol) {
    PtzCheck c;
    if (!s.verified) {
        c.result = PtzResult::Unverified;
        c.ok = false;
        c.text = "实际值不可读（未核验）";
        return c;
    }
    c.pan_delta = std::fabs(s.pan_actual - s.pan_target);
    c.tilt_delta = std::fabs(s.tilt_actual - s.tilt_target);
    c.zoom_delta = std::fabs(s.zoom_actual - s.zoom_target);
    const bool ok = c.pan_delta <= tol.pan_deg && c.tilt_delta <= tol.tilt_deg && c.zoom_delta <= tol.zoom;
    c.result = ok ? PtzResult::Ok : PtzResult::OutOfTolerance;
    c.ok = ok;
    c.text = "Pan Δ" + fmt2(c.pan_delta) + "° Tilt Δ" + fmt2(c.tilt_delta) + "° Zoom Δ" + fmt2(c.zoom_delta) +
             " → " + ptz_result_label(c.result);
    return c;
}

namespace {
PtzAction make_action(const std::string& kind, const std::string& label, const PtzState& before,
                      const PtzState& after, const PtzTolerance& tol) {
    PtzAction a;
    a.kind = kind;
    a.label = label;
    a.requires_confirm = true;
    a.applied = true;
    a.before = before;
    a.after = after;
    a.check = ptz_check(after, tol);
    return a;
}
}  // namespace

PtzAction ptz_home(const PtzState& before, const PtzState& after, const PtzTolerance& tol) {
    return make_action("home", "一键归位", before, after, tol);
}

PtzAction ptz_recall_front_preset(const PtzState& before, const PtzState& after, const PtzTolerance& tol) {
    return make_action("preset_front", "重调前方预置位", before, after, tol);
}

MaintenanceLogRow build_ptz_log(const PtzAction& a, const std::string& user,
                                long long ts_wall_ms, long long ts_mono_us) {
    return build_maintenance_log(a.kind, "ptz", a.check.ok ? "ok" : to_string(a.check.result),
                                 a.label + " " + a.check.text, user, ts_wall_ms, ts_mono_us);
}

MaintenanceLogRow build_maintenance_log(const std::string& action, const std::string& target,
                                        const std::string& result, const std::string& detail,
                                        const std::string& user, long long ts_wall_ms, long long ts_mono_us) {
    MaintenanceLogRow row;
    row.ts_wall_ms = ts_wall_ms;
    row.ts_mono_us = ts_mono_us;
    row.user = user;
    row.action = action;
    row.target = target;
    row.result = result;
    row.detail = detail;
    return row;
}

std::string log_day_key(long long ts_wall_ms) {
    return format_time_text(ts_wall_ms).substr(0, 10);   // "YYYY-MM-DD"
}

std::vector<MaintenanceLogRow> roll_logs_by_day(const std::vector<MaintenanceLogRow>& logs, int max_days) {
    // 收集出现的日键（时间序），仅保留最近 max_days 天。
    std::vector<std::string> days;
    for (const auto& r : logs) {
        const std::string d = log_day_key(r.ts_wall_ms);
        if (std::find(days.begin(), days.end(), d) == days.end()) days.push_back(d);
    }
    std::sort(days.begin(), days.end());   // YYYY-MM-DD 字典序 = 时间序
    std::vector<std::string> keep = days;
    if (max_days > 0 && static_cast<int>(keep.size()) > max_days)
        keep.erase(keep.begin(), keep.begin() + (static_cast<int>(keep.size()) - max_days));
    std::vector<MaintenanceLogRow> out;
    for (const auto& r : logs) {
        const std::string d = log_day_key(r.ts_wall_ms);
        if (std::find(keep.begin(), keep.end(), d) != keep.end()) out.push_back(r);
    }
    return out;
}

bool front_ai_ready(bool front_ptz_ok, bool front_ai_available) {
    return front_ptz_ok && front_ai_available;
}

std::string quality_label(const QualityFlags& q) {
    if (!q.any()) return "正常";
    std::string s;
    auto add = [&](const char* t) { if (!s.empty()) s += "/"; s += t; };
    if (q.too_dark) add("过暗");
    if (q.too_bright) add("过曝");
    if (q.occluded) add("遮挡");
    if (q.blurred) add("模糊");
    return s;
}

std::vector<CamMaintenanceRow> build_maintenance_cameras(const std::vector<CameraSlot>& registry,
                                                         const std::vector<CamRuntime>& runtimes,
                                                         long long now_ms, int stale_ms) {
    std::vector<CamMaintenanceRow> out;
    for (std::size_t i = 0; i < registry.size(); ++i) {
        const CameraSlot& c = registry[i];
        CamMaintenanceRow r;
        r.id = c.id;
        r.name = c.name;
        r.position = to_string(c.position);
        if (i < runtimes.size() && runtimes[i].has_stream) {
            const CamRuntime& rt = runtimes[i];
            r.state_label = channel_state_label(to_string(rt.state));
            r.fps_text = format_fps(rt.fps);
            if (rt.last_frame_ms >= 0) r.last_frame_text = format_last_frame(now_ms - rt.last_frame_ms);
            else r.last_frame_text = "—";
            r.quality_label = quality_label(rt.quality);
            r.online = (rt.state == StreamState::Live || rt.state == StreamState::Degraded);
            if (rt.reconnect_count > 0) r.reconnect_text = "重连 " + std::to_string(rt.reconnect_count) + " 次";
            // 帧陈旧即使状态未转也视为非在线
            if (rt.last_frame_ms >= 0 && (now_ms - rt.last_frame_ms) > stale_ms) r.online = false;
        } else {
            r.state_label = "未启动";
            r.fps_text = "—";
            r.last_frame_text = "—";
            r.quality_label = "—";
            r.online = false;
        }
        out.push_back(r);
    }
    return out;
}

RecordingStatus recording_status(bool running, long long free_bytes, long long total_bytes) {
    RecordingStatus s;
    s.running = running;
    s.free_bytes = free_bytes;
    s.total_bytes = total_bytes;
    if (total_bytes > 0) s.used_pct = static_cast<int>((total_bytes - free_bytes) * 100 / total_bytes);
    s.free_text = fmt_bytes(free_bytes);
    s.state_text = running ? "录像中" : "已停止";
    return s;
}

std::string health_text(const HealthSnapshot& h) {
    std::string s = "NPU ";
    s += (h.npu_load >= 0) ? (std::to_string(static_cast<int>(h.npu_load + 0.5)) + "%") : "—";
    s += " / CPU ";
    s += (h.cpu >= 0) ? (std::to_string(static_cast<int>(h.cpu + 0.5)) + "%") : "—";
    s += " / 温度 ";
    s += (h.temp_c >= 0) ? (std::to_string(static_cast<int>(h.temp_c + 0.5)) + "°C") : "—";
    s += " / 网络错误 " + std::to_string(h.net_errors);
    return s;
}

}  // namespace core
}  // namespace railhmi
