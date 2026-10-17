#pragma once

// railhmi-media: 视频接入管线工厂（视频 ITEM-002，target 专用）。
//
// 三类源（rtsp 强制 UDP / file / test）复用同一条 "源 → 解码 → video/x-raw,format=NV12"
// 管线；默认 decodebin 自适应 H.264/H.265，并可配置显式
// rtph264depay|rtph265depay + h26xparse + mppvideodec 固化路径。
// rtspsrc 的 latency/timeout/udp-buffer-size 与认证属性来自调用方配置；
// 凭据只作进程内 rtspsrc 属性（user-id/user-pw），不进命令行/日志；另提供整串脱敏辅助。
//
// 仅依赖 C++ 标准库（不 spawn GStreamer），便于按字符串做映射核对。

#include <string>

namespace railhmi {
namespace media {

enum class RenderPath { Cpu, Gl };

// 从字符串解析渲染路径（"cpu"/"gl"）；非法时 *ok=false。
RenderPath render_path_from_string(const std::string& s, bool* ok);

const char* render_path_name(RenderPath p);

// 源类型：三类源共用同一构建函数。
enum class PipelineSource { Test, File, Rtsp };
const char* pipeline_source_name(PipelineSource s);
bool parse_pipeline_source(const std::string& s, PipelineSource* out);

// 解码路径：Auto=decodebin 自适应；ExplicitH264/H265=显式 depay/parse/mppvideodec。
enum class DecodeMode { Auto, ExplicitH264, ExplicitH265 };
const char* decode_mode_name(DecodeMode m);
bool parse_decode_mode(const std::string& s, DecodeMode* out);

struct CameraPipelineSpec {
    PipelineSource source = PipelineSource::Test;
    std::string camera_id;

    // rtsp：url 可含 user:pass@（工厂拆分为认证属性）；也可用下方显式字段。
    std::string url;
    std::string username;
    std::string password;      // 仅内存使用，绝不打印
    int latency_ms = 500;
    int timeout_us = 5000000;
    int udp_buffer_bytes = 4194304;
    DecodeMode decode = DecodeMode::Auto;

    // file
    std::string file;

    // test（生成源）
    bool test_is_live = true;
    int test_width = 1280;
    int test_height = 720;
    int test_fps = 25;

    // 显示/分析分支
    RenderPath path = RenderPath::Cpu;
};

// 把 URL 中的 user:pass@ 拆到 username/password，*location 为不含用户信息的地址。
// 无用户信息时 *location=url 且返回 false。不做百分号转义/反转义。
bool split_rtsp_userinfo(const std::string& url, std::string* location,
                         std::string* username, std::string* password);

// 生成 "源 → 解码 → video/x-raw,format=NV12" 前缀（不含 tee/sink）。
std::string build_nv12_prefix(const CameraPipelineSpec& spec);

// 生成完整 pipeline 描述串：前缀 + tee + 计数 fakesink + 显示/分析 appsink|glsink。
// 命名为 frame_sink / count_sink / videosink，供 CameraStream 复用。
std::string build_pipeline_description(const CameraPipelineSpec& spec);

// 把 text 中出现的 secret 全部替换为 ***（错误日志安全）。secret 为空时原样返回。
std::string redact_secret(const std::string& text, const std::string& secret);

// 运行时可用的 GStreamer 版本（链接 gstreamer-1.0）。
std::string gstreamer_runtime_version();

// librga 使用的中间格式名（编译期常量，验证 im2d.h 可用）。
const char* rga_intermediate_format_name();

}  // namespace media
}  // namespace railhmi
