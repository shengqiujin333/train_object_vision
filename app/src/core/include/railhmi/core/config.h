#pragma once

// railhmi-core: 配置与状态契约层（ITEM-004）。
// 加载并校验 cameras.yaml / app.yaml / thresholds.yaml / roi.yaml；
// 缺失或非法字段报出**具体字段路径**，绝不为缺失的必填字段静默使用默认值；
// 可选字段的默认值记入 applied_defaults（可观察，不静默）。
// 暴露交互方案 v2 §7.2 的静态契约字段：ai_enabled / ai_profile /
// integration_state / mode / recording_source 等。
//
// 仅依赖 C++ 标准库；不含 Qt/GStreamer/相机/SQLite。

#include <cstdint>
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

// 权限相关配置（交互 §8：“受配置”的导出权限）。
struct PermissionsConfig {
    bool export_ops_allowed = false;   // 维护/研发是否允许导出（默认 false=仅管理员）
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

// BLE 门磁（USTONE BWD1 / USTONE-HA-BSM1）广播接入配置。
// 可选配置节 app.door_sensor；缺省记入 applied_defaults，不静默启用设备接入。
// 协议/硬件事实来自根目录两份厂商 PDF，见 railhmi/core/door_sensor.h。
struct DoorSensorDeviceConfig {
    std::string id;
    std::string name;
    std::string mac;                 // AA:BB:CC:DD:EE:FF（大小写不敏感）
    bool has_device_id = false;
    std::uint32_t device_id = 0;     // 广播帧 byte1..4 小端序设备 ID（可选核验）
};

struct DoorSensorConfig {
    bool enabled = false;            // 缺省 false：未配置时保持「未接入」，不虚构设备
    bool auto_discover = true;       // 允许按广播名发现；仍须通过 name_filter 闸门
    std::string name_filter = "T1"; // 使用说明：搜到名称 T1 表示设备进入工作状态
    std::string adapter;             // 空=自动选择第一个 org.bluez.Adapter1
    int poll_interval_ms = 1000;     // GetManagedObjects 轮询间隔；范围 1000..10000ms（现场 1s 快速读取）
    int stale_ms = 60000;            // 超过该时长未见广播变化 -> STALE/不可用
    int battery_low_pct = 20;        // 低电量阈值（百分比）
    int payload_offset = 0;          // 广播载荷前缀偏移；0=自动在前 4 字节内定位 0x07
    std::vector<DoorSensorDeviceConfig> devices;   // 已登记 MAC（权威来源）
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
    PermissionsConfig permissions;
    AiConfig ai;
    DoorSensorConfig door_sensor;
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
    int replay_sync_threshold_ms = 0;   // 三联回放同步偏差阈值
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

// 解析命令行 `--config <dir>` / `--config=<dir>`；未提供或无值时回退 fallback。
// 供部署单元（systemd ExecStart --config /opt/railhmi/config）真正消费该参数。
std::string parse_config_dir(int argc, const char* const* argv, const std::string& fallback);

// 外部设备（NVR 等）初始集成状态：已登记但缺 protocol/channel_mapping → Unconfigured，
// 其余待探测 → Unverified。探测/可达/不可达由运行时（ITEM-017）更新。
IntegrationState initial_integration_state(const IntegrationConfig& cfg);

// 本期仅接可见光；红外通道未接入。
bool mode_ir_connected();

}  // namespace core
}  // namespace railhmi
