#include "railhmi/core/config.h"

#include <fstream>
#include <set>
#include <sstream>

namespace railhmi {
namespace core {

// ---------------------------------------------------------------------------
// 枚举
// ---------------------------------------------------------------------------
const char* to_string(CameraPosition v) {
    switch (v) {
        case CameraPosition::Primary: return "primary";
        case CameraPosition::Left: return "left";
        case CameraPosition::Right: return "right";
        case CameraPosition::Roof: return "roof";
        case CameraPosition::Underfloor: return "underfloor";
        case CameraPosition::Cab: return "cab";
    }
    return "?";
}
const char* to_string(AiProfile v) {
    switch (v) {
        case AiProfile::Front: return "front";
        case AiProfile::Pantograph: return "pantograph";
        case AiProfile::WheelRail: return "wheel_rail";
        case AiProfile::CabDoor: return "cab_door";
        case AiProfile::None: return "none";
    }
    return "?";
}
const char* to_string(Spectrum v) { return v == Spectrum::Ir ? "ir" : "visible"; }
const char* to_string(RenderPath v) { return v == RenderPath::Gl ? "gl" : "cpu"; }
const char* to_string(ImagingMode v) { return v == ImagingMode::Ir ? "ir" : "visible"; }
const char* to_string(RecordingSource v) {
    switch (v) {
        case RecordingSource::Local: return "local";
        case RecordingSource::Nvr: return "nvr";
        case RecordingSource::Both: return "both";
    }
    return "?";
}
const char* to_string(IntegrationState v) {
    switch (v) {
        case IntegrationState::Probing: return "probing";
        case IntegrationState::Reachable: return "reachable";
        case IntegrationState::Unreachable: return "unreachable";
        case IntegrationState::Unconfigured: return "unconfigured";
        case IntegrationState::Unverified: return "unverified";
    }
    return "?";
}
const char* to_string(Role v) {
    switch (v) {
        case Role::Driver: return "driver";
        case Role::Maintainer: return "maintainer";
        case Role::Developer: return "developer";
        case Role::Admin: return "admin";
    }
    return "?";
}
const char* to_string(LogLevel v) {
    switch (v) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        case LogLevel::Error: return "error";
    }
    return "?";
}

bool parse_camera_position(const std::string& s, CameraPosition* out) {
    if (s == "primary") { *out = CameraPosition::Primary; return true; }
    if (s == "left") { *out = CameraPosition::Left; return true; }
    if (s == "right") { *out = CameraPosition::Right; return true; }
    if (s == "roof") { *out = CameraPosition::Roof; return true; }
    if (s == "underfloor") { *out = CameraPosition::Underfloor; return true; }
    if (s == "cab") { *out = CameraPosition::Cab; return true; }
    return false;
}
bool parse_ai_profile(const std::string& s, AiProfile* out) {
    if (s == "front") { *out = AiProfile::Front; return true; }
    if (s == "pantograph") { *out = AiProfile::Pantograph; return true; }
    if (s == "wheel_rail") { *out = AiProfile::WheelRail; return true; }
    if (s == "cab_door") { *out = AiProfile::CabDoor; return true; }
    if (s == "none") { *out = AiProfile::None; return true; }
    return false;
}
bool parse_spectrum(const std::string& s, Spectrum* out) {
    if (s == "visible") { *out = Spectrum::Visible; return true; }
    if (s == "ir") { *out = Spectrum::Ir; return true; }
    return false;
}
bool parse_render_path(const std::string& s, RenderPath* out) {
    if (s == "gl") { *out = RenderPath::Gl; return true; }
    if (s == "cpu") { *out = RenderPath::Cpu; return true; }
    return false;
}
bool parse_imaging_mode(const std::string& s, ImagingMode* out) {
    if (s == "visible") { *out = ImagingMode::Visible; return true; }
    if (s == "ir") { *out = ImagingMode::Ir; return true; }
    return false;
}
bool parse_recording_source(const std::string& s, RecordingSource* out) {
    if (s == "local") { *out = RecordingSource::Local; return true; }
    if (s == "nvr") { *out = RecordingSource::Nvr; return true; }
    if (s == "both") { *out = RecordingSource::Both; return true; }
    return false;
}
bool parse_role(const std::string& s, Role* out) {
    if (s == "driver") { *out = Role::Driver; return true; }
    if (s == "maintainer") { *out = Role::Maintainer; return true; }
    if (s == "developer") { *out = Role::Developer; return true; }
    if (s == "admin") { *out = Role::Admin; return true; }
    return false;
}
bool parse_log_level(const std::string& s, LogLevel* out) {
    if (s == "debug") { *out = LogLevel::Debug; return true; }
    if (s == "info") { *out = LogLevel::Info; return true; }
    if (s == "warn") { *out = LogLevel::Warn; return true; }
    if (s == "error") { *out = LogLevel::Error; return true; }
    return false;
}

IntegrationState initial_integration_state(const IntegrationConfig& cfg) {
    if (!cfg.registered) return IntegrationState::Unverified;
    if (!cfg.has_protocol || !cfg.has_channel_mapping) return IntegrationState::Unconfigured;
    return IntegrationState::Unverified;  // 待运行时探测
}

bool mode_ir_connected() { return false; }  // 本期仅可见光

// ---------------------------------------------------------------------------
// 校验辅助
// ---------------------------------------------------------------------------
namespace {

using yaml::Node;
typedef std::vector<ConfigError> Errors;

void err(Errors& e, const std::string& path, const std::string& msg) {
    e.push_back(ConfigError{path, msg});
}

const Node* child(const Node& m, const std::string& key) { return m.get(key); }

bool req_string(const Node& m, const std::string& key, const std::string& base,
                std::string* out, Errors& e) {
    const std::string path = base + "." + key;
    const Node* n = child(m, key);
    if (!n || n->is_null()) { err(e, path, "required field missing"); return false; }
    if (!n->is_scalar()) { err(e, path, "expected scalar string"); return false; }
    *out = n->scalar;
    if (out->empty()) { err(e, path, "required field is empty"); return false; }
    return true;
}

bool opt_string(const Node& m, const std::string& key, const std::string& base,
                std::string* out, Errors& e) {
    const std::string path = base + "." + key;
    const Node* n = child(m, key);
    if (!n || n->is_null()) return false;
    if (!n->is_scalar()) { err(e, path, "expected scalar string"); return false; }
    *out = n->scalar;
    return true;
}

bool req_int(const Node& m, const std::string& key, const std::string& base,
             long long* out, Errors& e) {
    const std::string path = base + "." + key;
    const Node* n = child(m, key);
    if (!n || n->is_null()) { err(e, path, "required field missing"); return false; }
    if (!yaml::to_int(*n, out)) { err(e, path, "expected integer"); return false; }
    return true;
}

bool req_double(const Node& m, const std::string& key, const std::string& base,
                double* out, Errors& e) {
    const std::string path = base + "." + key;
    const Node* n = child(m, key);
    if (!n || n->is_null()) { err(e, path, "required field missing"); return false; }
    if (!yaml::to_double(*n, out)) { err(e, path, "expected number"); return false; }
    return true;
}

bool req_bool(const Node& m, const std::string& key, const std::string& base,
              bool* out, Errors& e) {
    const std::string path = base + "." + key;
    const Node* n = child(m, key);
    if (!n || n->is_null()) { err(e, path, "required field missing"); return false; }
    if (!yaml::to_bool(*n, out)) { err(e, path, "expected boolean true/false"); return false; }
    return true;
}

// 读必填字符串并解析为枚举
template <typename T>
bool req_enum(const Node& m, const std::string& key, const std::string& base,
              bool (*parse)(const std::string&, T*), T* out, Errors& e) {
    const std::string path = base + "." + key;
    std::string raw;
    if (!req_string(m, key, base, &raw, e)) return false;
    if (!parse(raw, out)) {
        err(e, path, "invalid value '" + raw + "'");
        return false;
    }
    return true;
}

bool require_map(const Node& n, const std::string& path, Errors& e) {
    if (!n.is_map()) { err(e, path, "expected mapping"); return false; }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// 解析各文件
// ---------------------------------------------------------------------------
namespace {

void load_cameras(const Node& root, AppSettings* s, Errors& e) {
    const Node* cams = root.get("cameras");
    if (!cams || cams->is_null()) { err(e, "cameras", "required field missing"); return; }
    if (!cams->is_seq()) { err(e, "cameras", "expected sequence of camera mappings"); return; }
    std::set<std::string> ids;
    for (std::size_t i = 0; i < cams->seq.size(); ++i) {
        const Node& c = cams->seq[i];
        const std::string base = "cameras[" + std::to_string(i) + "]";
        if (!require_map(c, base, e)) continue;
        CameraConfig cam;
        req_string(c, "id", base, &cam.id, e);
        req_string(c, "name", base, &cam.name, e);
        req_enum<CameraPosition>(c, "position", base, parse_camera_position, &cam.position, e);
        req_string(c, "rtsp_url", base, &cam.rtsp_url, e);
        req_string(c, "transport", base, &cam.transport, e);
        req_string(c, "username", base, &cam.username, e);
        req_string(c, "credential_env", base, &cam.credential_env, e);
        req_enum<Spectrum>(c, "spectrum", base, parse_spectrum, &cam.spectrum, e);
        req_bool(c, "ai_enabled", base, &cam.ai_enabled, e);
        req_enum<AiProfile>(c, "ai_profile", base, parse_ai_profile, &cam.ai_profile, e);
        long long fps = 0;
        if (req_int(c, "display_fps", base, &fps, e)) {
            if (fps < 1 || fps > 60) err(e, base + ".display_fps", "out of range 1..60");
            else cam.display_fps = static_cast<int>(fps);
        }
        if (!opt_string(c, "tile_aspect", base, &cam.tile_aspect, e) || cam.tile_aspect.empty()) {
            cam.tile_aspect = "16:9";
            s->applied_defaults.push_back(base + ".tile_aspect");
        }
        // 值域/一致性校验
        if (!cam.rtsp_url.empty() && cam.rtsp_url.rfind("rtsp://", 0) != 0)
            err(e, base + ".rtsp_url", "must start with rtsp://");
        if (!cam.transport.empty() && cam.transport != "udp")
            err(e, base + ".transport", "only 'udp' is supported, got '" + cam.transport + "'");
        if (!cam.id.empty() && !ids.insert(cam.id).second)
            err(e, base + ".id", "duplicate camera id '" + cam.id + "'");
        // ai_enabled 与 ai_profile 一致性
        const bool has_profile = (cam.ai_profile != AiProfile::None);
        if (cam.ai_enabled && !has_profile)
            err(e, base + ".ai_profile", "ai_enabled=true requires a non-none ai_profile");
        if (!cam.ai_enabled && has_profile)
            err(e, base + ".ai_profile", "ai_enabled=false requires ai_profile=none");
        s->cameras.push_back(cam);
    }
}

void load_app(const Node& root, AppSettings* s, Errors& e) {
    const Node* app = root.get("app");
    if (!app || app->is_null()) { err(e, "app", "required field missing"); return; }
    if (!require_map(*app, "app", e)) return;
    AppConfig& a = s->app;
    req_string(*app, "language", "app", &a.language, e);
    req_enum<Role>(*app, "default_role", "app", parse_role, &a.default_role, e);
    req_enum<RenderPath>(*app, "render_path", "app", parse_render_path, &a.render_path, e);
    req_enum<ImagingMode>(*app, "mode", "app", parse_imaging_mode, &a.mode, e);
    req_enum<RecordingSource>(*app, "recording_source", "app", parse_recording_source,
                              &a.recording_source, e);
    req_enum<LogLevel>(*app, "log_level", "app", parse_log_level, &a.log_level, e);
    req_bool(*app, "simulation", "app", &a.simulation, e);
    // resolution: [w, h]
    const Node* res = app->get("resolution");
    if (!res || res->is_null()) {
        err(e, "app.resolution", "required field missing");
    } else if (!res->is_seq() || res->seq.size() != 2) {
        err(e, "app.resolution", "expected flow sequence [width, height]");
    } else {
        long long w = 0, h = 0;
        if (!yaml::to_int(res->seq[0], &w) || !yaml::to_int(res->seq[1], &h)) {
            err(e, "app.resolution", "expected integer width/height");
        } else if (w < 640 || h < 480) {
            err(e, "app.resolution", "resolution too small");
        } else {
            a.resolution_w = static_cast<int>(w);
            a.resolution_h = static_cast<int>(h);
        }
    }
    // storage
    const Node* st = app->get("storage");
    if (!st || st->is_null()) {
        err(e, "app.storage", "required field missing");
    } else if (require_map(*st, "app.storage", e)) {
        long long rd = 0;
        if (req_int(*st, "retention_days", "app.storage", &rd, e)) a.storage.retention_days = static_cast<int>(rd);
        req_double(*st, "max_gb", "app.storage", &a.storage.max_gb, e);
        req_double(*st, "low_threshold_pct", "app.storage", &a.storage.low_threshold_pct, e);
        if (a.storage.retention_days < 1) err(e, "app.storage.retention_days", "must be >= 1");
        if (a.storage.max_gb <= 0) err(e, "app.storage.max_gb", "must be > 0");
        if (a.storage.low_threshold_pct <= 0 || a.storage.low_threshold_pct >= 100)
            err(e, "app.storage.low_threshold_pct", "must be in (0,100)");
    }
    // ai（可选）：推理调度配置；缺省时记录默认值，不静默。
    const Node* ai = app->get("ai");
    if (ai && !ai->is_null()) {
        if (require_map(*ai, "app.ai", e)) {
            long long iv = 0, qd = 0, budget = 0;
            if (req_int(*ai, "inference_interval", "app.ai", &iv, e)) {
                if (iv < 1) err(e, "app.ai.inference_interval", "must be >= 1");
                else a.ai.inference_interval = static_cast<int>(iv);
            }
            if (req_int(*ai, "queue_depth", "app.ai", &qd, e)) {
                if (qd < 1 || qd > 8) err(e, "app.ai.queue_depth", "must be in [1,8]");
                else a.ai.queue_depth = static_cast<int>(qd);
            }
            if (req_int(*ai, "npu_budget", "app.ai", &budget, e)) {
                if (budget < 1) err(e, "app.ai.npu_budget", "must be >= 1");
                else a.ai.npu_budget = static_cast<int>(budget);
            }
            req_double(*ai, "temp_limit_c", "app.ai", &a.ai.temp_limit_c, e);
            if (a.ai.temp_limit_c <= 0) err(e, "app.ai.temp_limit_c", "must be > 0");
            const Node* models = ai->get("models");
            if (models && !models->is_null()) {
                if (!models->is_seq()) {
                    err(e, "app.ai.models", "expected sequence");
                } else {
                    for (std::size_t i = 0; i < models->seq.size(); ++i) {
                        const Node& mnode = models->seq[i];
                        const std::string mbase = "app.ai.models[" + std::to_string(i) + "]";
                        if (!require_map(mnode, mbase, e)) continue;
                        AiModelConfig mc;
                        req_enum<AiProfile>(mnode, "profile", mbase, parse_ai_profile, &mc.profile, e);
                        req_string(mnode, "name", mbase, &mc.name, e);
                        req_string(mnode, "version", mbase, &mc.version, e);
                        a.ai.models.push_back(mc);
                    }
                }
            }
        }
    } else {
        s->applied_defaults.push_back("app.ai");
    }
    // integrations（可选）
    const Node* ints = app->get("integrations");
    if (ints && !ints->is_null()) {
        if (!ints->is_seq()) {
            err(e, "app.integrations", "expected sequence");
        } else {
            for (std::size_t i = 0; i < ints->seq.size(); ++i) {
                const Node& in = ints->seq[i];
                const std::string base = "app.integrations[" + std::to_string(i) + "]";
                if (!require_map(in, base, e)) continue;
                IntegrationConfig cfg;
                req_string(in, "id", base, &cfg.id, e);
                req_string(in, "name", base, &cfg.name, e);
                req_string(in, "address", base, &cfg.address, e);
                req_bool(in, "registered", base, &cfg.registered, e);
                cfg.has_protocol = opt_string(in, "protocol", base, &cfg.protocol, e);
                std::string chan;
                cfg.has_channel_mapping = opt_string(in, "channel_mapping", base, &chan, e);
                a.integrations.push_back(cfg);
            }
        }
    } else {
        s->applied_defaults.push_back("app.integrations");
    }
}

void load_thresholds(const Node& root, AppSettings* s, Errors& e) {
    const Node* th = root.get("thresholds");
    if (!th || th->is_null()) { err(e, "thresholds", "required field missing"); return; }
    if (!require_map(*th, "thresholds", e)) return;
    ThresholdsConfig& t = s->thresholds;
    long long v = 0;
    if (req_int(*th, "confirm_frames", "thresholds", &v, e)) {
        if (v < 1) err(e, "thresholds.confirm_frames", "must be >= 1"); else t.confirm_frames = static_cast<int>(v);
    }
    if (req_int(*th, "confirm_window_ms", "thresholds", &v, e)) {
        if (v < 0) err(e, "thresholds.confirm_window_ms", "must be >= 0"); else t.confirm_window_ms = static_cast<int>(v);
    }
    req_double(*th, "danger_proximity_m", "thresholds", &t.danger_proximity_m, e);
    req_double(*th, "confidence_min", "thresholds", &t.confidence_min, e);
    if (req_int(*th, "suppress_window_ms", "thresholds", &v, e)) {
        if (v < 0) err(e, "thresholds.suppress_window_ms", "must be >= 0"); else t.suppress_window_ms = static_cast<int>(v);
    }
    if (req_int(*th, "first_frame_timeout_ms", "thresholds", &v, e)) {
        if (v < 1) err(e, "thresholds.first_frame_timeout_ms", "must be >= 1"); else t.first_frame_timeout_ms = static_cast<int>(v);
    }
    if (req_int(*th, "freeze_timeout_ms", "thresholds", &v, e)) {
        if (v < 1) err(e, "thresholds.freeze_timeout_ms", "must be >= 1"); else t.freeze_timeout_ms = static_cast<int>(v);
    }
    if (req_int(*th, "reconnect_threshold", "thresholds", &v, e)) {
        if (v < 1) err(e, "thresholds.reconnect_threshold", "must be >= 1"); else t.reconnect_threshold = static_cast<int>(v);
    }
    if (req_int(*th, "reconnect_backoff_ms", "thresholds", &v, e)) {
        if (v < 0) err(e, "thresholds.reconnect_backoff_ms", "must be >= 0"); else t.reconnect_backoff_ms = static_cast<int>(v);
    }
    req_double(*th, "storage_low_pct", "thresholds", &t.storage_low_pct, e);
    if (req_int(*th, "clip_pre_seconds", "thresholds", &v, e)) {
        if (v < 0) err(e, "thresholds.clip_pre_seconds", "must be >= 0"); else t.clip_pre_seconds = static_cast<int>(v);
    }
    if (req_int(*th, "clip_post_seconds", "thresholds", &v, e)) {
        if (v < 0) err(e, "thresholds.clip_post_seconds", "must be >= 0"); else t.clip_post_seconds = static_cast<int>(v);
    }
    if (t.danger_proximity_m <= 0) err(e, "thresholds.danger_proximity_m", "must be > 0");
    if (t.confidence_min < 0 || t.confidence_min > 1) err(e, "thresholds.confidence_min", "must be in [0,1]");
    if (t.storage_low_pct <= 0 || t.storage_low_pct >= 100) err(e, "thresholds.storage_low_pct", "must be in (0,100)");
}

void load_roi(const Node& root, AppSettings* s, Errors& e) {
    const Node* roi = root.get("roi");
    if (!roi || roi->is_null()) { err(e, "roi", "required field missing"); return; }
    if (!roi->is_seq()) { err(e, "roi", "expected sequence of roi mappings"); return; }
    std::set<std::string> cam_ids;
    for (const auto& c : s->cameras) cam_ids.insert(c.id);
    for (std::size_t i = 0; i < roi->seq.size(); ++i) {
        const Node& r = roi->seq[i];
        const std::string base = "roi[" + std::to_string(i) + "]";
        if (!require_map(r, base, e)) continue;
        RoiConfig rc;
        req_string(r, "camera", base, &rc.camera, e);
        req_string(r, "name", base, &rc.name, e);
        req_double(r, "x", base, &rc.x, e);
        req_double(r, "y", base, &rc.y, e);
        req_double(r, "w", base, &rc.w, e);
        req_double(r, "h", base, &rc.h, e);
        if (!rc.camera.empty() && cam_ids.find(rc.camera) == cam_ids.end())
            err(e, base + ".camera", "unknown camera id '" + rc.camera + "'");
        const std::string vals[4] = {"x", "y", "w", "h"};
        const double vv[4] = {rc.x, rc.y, rc.w, rc.h};
        for (int k = 0; k < 4; ++k)
            if (vv[k] < 0.0 || vv[k] > 1.0) err(e, base + "." + vals[k], "must be in [0,1]");
        if (rc.x + rc.w > 1.0 + 1e-9) err(e, base, "x + w must be <= 1");
        if (rc.y + rc.h > 1.0 + 1e-9) err(e, base, "y + h must be <= 1");
        s->roi.push_back(rc);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------
namespace {

bool parse_file(const std::string& name, const std::string& text, Node* out, Errors& e) {
    std::string perr;
    if (!yaml::parse(text, out, &perr)) {
        err(e, name, "YAML parse error: " + perr);
        return false;
    }
    return true;
}

}  // namespace

ConfigLoadResult load_config_from_strings(const std::string& cameras_yaml,
                                          const std::string& app_yaml,
                                          const std::string& thresholds_yaml,
                                          const std::string& roi_yaml) {
    ConfigLoadResult result;
    Node cameras, app, thresholds, roi;
    if (!parse_file("cameras.yaml", cameras_yaml, &cameras, result.errors)) { /* keep going */ }
    if (!parse_file("app.yaml", app_yaml, &app, result.errors)) { /* keep going */ }
    if (!parse_file("thresholds.yaml", thresholds_yaml, &thresholds, result.errors)) { /* keep going */ }
    if (!parse_file("roi.yaml", roi_yaml, &roi, result.errors)) { /* keep going */ }

    if (!cameras.is_null()) load_cameras(cameras, &result.settings, result.errors);
    if (!app.is_null()) load_app(app, &result.settings, result.errors);
    if (!thresholds.is_null()) load_thresholds(thresholds, &result.settings, result.errors);
    if (!roi.is_null()) load_roi(roi, &result.settings, result.errors);

    result.ok = result.errors.empty();
    return result;
}

ConfigLoadResult load_config_dir(const std::string& dir) {
    ConfigLoadResult result;
    auto read = [&](const std::string& name, std::string* out) -> bool {
        std::string path = dir;
        if (!path.empty() && path.back() != '/' && path.back() != '\\') path += "/";
        path += name;
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            err(result.errors, name, "file not found: " + path);
            return false;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        *out = ss.str();
        return true;
    };
    std::string c, a, t, r;
    bool ok = true;
    ok = read("cameras.yaml", &c) && ok;
    ok = read("app.yaml", &a) && ok;
    ok = read("thresholds.yaml", &t) && ok;
    ok = read("roi.yaml", &r) && ok;
    if (!ok) {
        result.ok = false;
        return result;
    }
    return load_config_from_strings(c, a, t, r);
}

}  // namespace core
}  // namespace railhmi
