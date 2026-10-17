#pragma once

// railhmi-media: 视频源能力探测与配置绑定（视频 ITEM-001）。
//
// 职责：
//   * 从 cameras.yaml/app.yaml/thresholds.yaml 绑定六路 RTSP 登记项
//     （URL 不含凭据；凭据仅经 credential_env 在进程内读取，只判存在性）。
//   * 接入前用**直接** `gst-inspect-1.0 <element>` 判定元素可用性并解析
//     Long-name；**绝不**依赖 `gst-inspect-1.0 --exists` 退出码（实测会假否）。
//   * 记录 GStreamer 运行时版本、librga API 版本、librknnrt 版本。
//   * 逐路产出能力记录（凭据状态、就绪性、协商 caps 或待实测）。
//   * 缺凭据返回 CREDENTIAL_MISSING；缺元素显式列出元素名；报告绝不含凭据。
//
// 目标专用（依赖 GStreamer/librga）。纯逻辑辅助（元素输出解析、凭据判定、
// 就绪性判定）与进程执行分离，便于最小工具链下的单元测试。

#include <string>
#include <vector>

#include "railhmi/core/config.h"

namespace railhmi {
namespace media {

enum class CredentialStatus { Present, Missing };
const char* to_string(CredentialStatus s);

enum class ElementStatus { Present, Absent };
const char* to_string(ElementStatus s);

struct ElementProbeResult {
    std::string element;
    ElementStatus status = ElementStatus::Absent;
    std::string long_name;   // 来自 gst-inspect "Long-name"
    std::string detail;      // 有界诊断（缺失/超时/错误原因），不含凭据
};

struct LibraryVersions {
    std::string gstreamer;   // 运行时 gst_version()
    std::string librga;      // 编译期 RGA_API_FULL_VERSION
    std::string librknnrt;   // 从共享库提取；未知时显式 "unknown(...)"
};

// 协商 caps 记录；未实测时为 "to_be_measured"，不写死文档预期。
struct CapsRecord {
    std::string status = "to_be_measured";   // to_be_measured|measured|timeout|error
    std::string codec;                       // RTP encoding-name（H265/H264/...）
    std::string format;                      // 解码后 raw 格式（NV12/...）
    int width = 0;
    int height = 0;
    int fps_num = 0;
    int fps_den = 1;
    std::string colorimetry;
    std::string detail;                      // 有界、已脱敏
};

struct CameraCapability {
    std::string camera_id;
    std::string name;
    std::string position;
    std::string spectrum;
    bool ai_enabled = false;
    std::string ai_profile;
    int display_fps = 0;
    std::string rtsp_url;          // 配置中的 URL（不含凭据）
    std::string credential_env;    // 仅环境变量"名"，不含值
    CredentialStatus credential = CredentialStatus::Missing;
    std::string readiness;         // ready | blocked_credential_missing | blocked_element_missing:<el>
    CapsRecord caps;
};

struct CapabilityReport {
    bool config_ok = false;
    std::vector<core::ConfigError> config_errors;
    std::vector<std::string> applied_defaults;
    std::string render_path;
    std::string recording_source;
    LibraryVersions versions;
    std::vector<ElementProbeResult> elements;
    bool required_elements_present = false;
    std::vector<std::string> missing_required_elements;
    std::vector<CameraCapability> cameras;
};

// ---- 纯逻辑辅助（可单元测试，不依赖 GStreamer） ----
std::vector<std::string> required_gstreamer_elements();

// 解析 `gst-inspect-1.0 <element>` 输出/退出码。Present 需 exit==0 且含
// "Factory Details:"；否则 Absent，并把首行诊断放入 detail。不使用 --exists。
bool parse_gst_inspect(const std::string& element, const std::string& output,
                       int exit_code, ElementProbeResult* out);

// 凭据只判存在性：env_lookup 返回该环境变量的值；空/未设置 -> Missing。
// 实现绝不把返回值写入报告。
CredentialStatus resolve_credential_env(const std::string& env_name,
                                        bool (*env_lookup)(const std::string&));

// 就绪性：缺凭据优先；否则任一必需元素缺失即 blocked；否则 ready。
std::string evaluate_readiness(CredentialStatus credential,
                               const std::vector<std::string>& missing_elements);

// ---- 目标执行 ----
ElementProbeResult probe_gstreamer_element(const std::string& element, int timeout_sec);
LibraryVersions probe_library_versions();
CapsRecord probe_camera_caps(const core::CameraConfig& cam, int timeout_ms);

// 组装完整能力报告；with_caps_probe 时对就绪路执行有界 caps 探测。
CapabilityReport build_capability_report(const core::AppSettings& settings,
                                         bool with_caps_probe, int caps_timeout_ms);

std::string capability_report_to_json(const CapabilityReport& r);
std::string capability_report_to_text(const CapabilityReport& r);

}  // namespace media
}  // namespace railhmi
