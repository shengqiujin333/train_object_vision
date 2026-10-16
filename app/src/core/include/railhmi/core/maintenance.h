#pragma once

// railhmi-core: 设备维护页模型（ITEM-022 / INTERACTION §5.6、VISUAL §8.6）。
// 摄像头维护行（在线/状态/帧率/最近帧/质量）、PTZ Pan/Tilt/Zoom 实际值 vs 目标值 + 容差校验、
// 一键归位/重调前方预置位（二次确认，写入维护日志）、超差 -> 前方 AI 未就绪、
// 录像/存储、网络/温度/负载、NVR 通道映射未配置呈现。纯逻辑，无 Qt/SQLite。

#include <string>
#include <vector>

#include "railhmi/core/camera_registry.h"
#include "railhmi/core/channel_view.h"
#include "railhmi/core/config.h"
#include "railhmi/core/persistence_model.h"
#include "railhmi/core/stream_state.h"

namespace railhmi {
namespace core {

// ---------------- 云台 PTZ ----------------
struct PtzTolerance {
    double pan_deg = 1.0;
    double tilt_deg = 1.0;
    double zoom = 0.1;
};
struct PtzState {
    double pan_actual = 0, tilt_actual = 0, zoom_actual = 0;
    double pan_target = 0, tilt_target = 0, zoom_target = 0;
    bool verified = true;   // 实际值可读（不可读 -> 未核验）
};
enum class PtzResult { Ok, OutOfTolerance, Unverified };
const char* to_string(PtzResult r);
const char* ptz_result_label(PtzResult r);   // 容差内 / 超差 / 未核验

struct PtzCheck {
    PtzResult result = PtzResult::Unverified;
    bool ok = false;
    double pan_delta = 0, tilt_delta = 0, zoom_delta = 0;
    std::string text;
};
PtzCheck ptz_check(const PtzState& s, const PtzTolerance& tol);

// 二次确认动作（归位 / 调前方预置位）。
struct PtzAction {
    std::string kind;          // home | preset_front
    std::string label;         // 一键归位 / 重调前方预置位
    bool requires_confirm = true;
    bool applied = false;      // 二次确认后已执行
    PtzState before, after;
    PtzCheck check;
};
PtzAction ptz_home(const PtzState& before, const PtzState& after, const PtzTolerance& tol);
PtzAction ptz_recall_front_preset(const PtzState& before, const PtzState& after, const PtzTolerance& tol);
MaintenanceLogRow build_ptz_log(const PtzAction& a, const std::string& user,
                                long long ts_wall_ms, long long ts_mono_us);

// 通用维护日志条目（重连/归位/参数变更/模型升级/导出等）。
MaintenanceLogRow build_maintenance_log(const std::string& action, const std::string& target,
                                        const std::string& result, const std::string& detail,
                                        const std::string& user, long long ts_wall_ms, long long ts_mono_us);
// 日志日键（UTC，YYYY-MM-DD）。
std::string log_day_key(long long ts_wall_ms);
// 按日滚动：仅保留最近 max_days 天（按日键）的日志，保持时间序（ITEM-022 D-02）。
std::vector<MaintenanceLogRow> roll_logs_by_day(const std::vector<MaintenanceLogRow>& logs, int max_days);

// 前方 AI 就绪：云台容差通过 且 前方 AI 可用；超差 -> 未就绪（交互 §5.6）。
bool front_ai_ready(bool front_ptz_ok, bool front_ai_available);

// ---------------- 每路维护行 ----------------
struct CamRuntime {
    bool has_stream = false;
    StreamState state = StreamState::Offline;
    double fps = 0;
    long long last_frame_ms = -1;
    QualityFlags quality;
    int reconnect_count = 0;
};
struct CamMaintenanceRow {
    std::string id, name, position;
    bool online = false;
    std::string state_label, fps_text, last_frame_text, quality_label, reconnect_text;
};
std::string quality_label(const QualityFlags& q);
std::vector<CamMaintenanceRow> build_maintenance_cameras(const std::vector<CameraSlot>& registry,
                                                         const std::vector<CamRuntime>& runtimes,
                                                         long long now_ms, int stale_ms = 3000);

// ---------------- 录像/存储 ----------------
struct RecordingStatus {
    bool running = false;
    long long free_bytes = 0, total_bytes = 0;
    int used_pct = 0;
    std::string free_text;
    std::string state_text;
};
RecordingStatus recording_status(bool running, long long free_bytes, long long total_bytes);

// ---------------- 网络/温度/负载 ----------------
struct HealthSnapshot {
    double npu_load = -1, cpu = -1, temp_c = -1;   // 百分比 / 摄氏度；-1=不可用
    long long net_errors = 0;
};
std::string health_text(const HealthSnapshot& h);

}  // namespace core
}  // namespace railhmi
