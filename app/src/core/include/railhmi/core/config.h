#pragma once

// railhmi-core: 配置与状态契约层（ITEM-004）。
// 加载并校验 cameras.yaml / app.yaml / thresholds.yaml / roi.yaml；
// 缺失或非法字段报出**具体字段路径**，绝不为缺失的必填字段静默使用默认值；
// 可选字段的默认值记入 applied_defaults（可观察，不静默）。
// 暴露交互方案 v2 §7.2 的静态契约字段：ai_enabled / ai_profile /
// integration_state / mode / recording_source 等。
//
// 仅依赖 C++ 标准库；不含 Qt/GStreamer/相机/SQLite。

#include <string>
#include <vector>

#include "railhmi/core/yaml.h"

namespace railhmi {
namespace core {

enum class CameraPosition { Primary, Left, Right, Roof, Underfloor, Cab };
enum class AiProfile { Front, Pantograph, WheelRail, CabDoor, None };
enum class Spectrum { Visible, Ir };
enum class RenderPath { Gl, Cpu };
enum class ImagingMode { Visible, Ir };
enum class RecordingSource { Local, Nvr, Both };
enum class IntegrationState { Probing, Reachable, Unreachable, Unconfigured, Unverified };
enum class Role { Driver, Maintainer, Developer, Admin };
enum class LogLevel { Debug, Info, Warn, Error };

const char* to_string(CameraPosition v);
const char* to_string(AiProfile v);
const char* to_string(Spectrum v);
const char* to_string(RenderPath v);
const char* to_string(ImagingMode v);
const char* to_string(RecordingSource v);
const char* to_string(IntegrationState v);
const char* to_string(Role v);
const char* to_string(LogLevel v);

bool parse_camera_position(const std::string& s, CameraPosition* out);
bool parse_ai_profile(const std::string& s, AiProfile* out);
bool parse_spectrum(const std::string& s, Spectrum* out);
bool parse_render_path(const std::string& s, RenderPath* out);
bool parse_imaging_mode(const std::string& s, ImagingMode* out);
bool parse_recording_source(const std::string& s, RecordingSource* out);
bool parse_role(const std::string& s, Role* out);
bool parse_log_level(const std::string& s, LogLevel* out);

struct CameraConfig {
    std::string id;
    std::string name;
    CameraPosition position = CameraPosition::Primary;
    std::string rtsp_url;
    std::string transport;          // 仅支持 "udp"
    std::string username;
    std::string credential_env;     // 环境变量名，URL 不含凭据
    Spectrum spectrum = Spectrum::Visible;
    bool ai_enabled = false;
    AiProfile ai_profile = AiProfile::None;
    int display_fps = 25;
    std::string tile_aspect;        // 可选，默认 "16:9"（记入 applied_defaults）
};

struct IntegrationConfig {
    std::string id;
    std::string name;
    std::string address;
    bool registered = false;
    bool has_protocol = false;
    std::string protocol;
    bool has_channel_mapping = false;
};

struct StorageConfig {
    int retention_days = 0;
    double max_gb = 0.0;
    double low_threshold_pct = 0.0;
};

// 推理调度配置（DESKTOP §8.2）：帧间隔、有界丢旧帧队列深度、NPU 预算、温度守卫。
// 模型版本按 ai_profile 登记（应用只消费模型名/版本，不定义精度）。
struct AiModelConfig {
    AiProfile profile = AiProfile::None;
    std::string name;
    std::string version;
};
struct AiConfig {
    int inference_interval = 5;   // 每 N 帧推理一次
    int queue_depth = 2;          // 有界丢旧帧队列深度（1..8）
    int npu_budget = 4;           // 并发推理路数预算
    double temp_limit_c = 85.0;   // 温度守卫阈值（超过则降载，不静默停）
    std::vector<AiModelConfig> models;
};

struct AppConfig {
    std::string language;
    Role default_role = Role::Driver;
    int resolution_w = 0;
    int resolution_h = 0;
    RenderPath render_path = RenderPath::Cpu;
    ImagingMode mode = ImagingMode::Visible;
    RecordingSource recording_source = RecordingSource::Both;
    LogLevel log_level = LogLevel::Info;
    bool simulation = false;
    StorageConfig storage;
    AiConfig ai;
    std::vector<IntegrationConfig> integrations;
};

struct ThresholdsConfig {
    int confirm_frames = 0;
    int confirm_window_ms = 0;
    double danger_proximity_m = 0.0;
    double confidence_min = 0.0;
    int suppress_window_ms = 0;
    int first_frame_timeout_ms = 0;
    int freeze_timeout_ms = 0;
    int reconnect_threshold = 0;
    int reconnect_backoff_ms = 0;
    double storage_low_pct = 0.0;
    int clip_pre_seconds = 0;
    int clip_post_seconds = 0;
};

struct RoiConfig {
    std::string camera;
    std::string name;
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

struct ConfigError {
    std::string path;      // 例如 "cameras[1].rtsp_url"
    std::string message;
    std::string str() const { return path + ": " + message; }
};

struct AppSettings {
    std::vector<CameraConfig> cameras;
    AppConfig app;
    ThresholdsConfig thresholds;
    std::vector<RoiConfig> roi;
    std::vector<std::string> applied_defaults;   // 被赋予文档化默认值的字段路径
};

struct ConfigLoadResult {
    bool ok = false;
    AppSettings settings;
    std::vector<ConfigError> errors;
};

// 从内存中的文件内容加载（便于单元测试）。
ConfigLoadResult load_config_from_strings(const std::string& cameras_yaml,
                                          const std::string& app_yaml,
                                          const std::string& thresholds_yaml,
                                          const std::string& roi_yaml);

// 从目录读取 4 个文件；文件缺失记为错误。
ConfigLoadResult load_config_dir(const std::string& dir);

// 外部设备（NVR 等）初始集成状态：已登记但缺 protocol/channel_mapping → Unconfigured，
// 其余待探测 → Unverified。探测/可达/不可达由运行时（ITEM-017）更新。
IntegrationState initial_integration_state(const IntegrationConfig& cfg);

// 本期仅接可见光；红外通道未接入。
bool mode_ir_connected();

}  // namespace core
}  // namespace railhmi
