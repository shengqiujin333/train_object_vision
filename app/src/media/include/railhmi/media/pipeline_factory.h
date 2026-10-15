#pragma once

// railhmi-media: 视频接入/渲染管线描述（target 专用）。
// 复用现有 code/main.cc 的取流语义：强制 UDP、原生 GStreamer、NV12；
// 渲染路径按 ITEM-002 实测选定 cpu（librga NV12->BGR），gl(qmlglsink) 保留为可开启候选。

#include <string>

namespace railhmi {
namespace media {

enum class RenderPath { Cpu, Gl };

// 从字符串解析渲染路径（"cpu"/"gl"）；非法时 *ok=false。
RenderPath render_path_from_string(const std::string& s, bool* ok);

const char* render_path_name(RenderPath p);

struct CameraPipelineSpec {
    std::string url;          // 已含凭据的 RTSP URL（调用方负责内存安全，日志须脱敏）
    RenderPath path = RenderPath::Cpu;
    int latency_ms = 500;
    int udp_buffer_bytes = 4194304;
};

// 生成 pipeline 描述串（GStreamer 语法）。不含凭据日志。
std::string build_pipeline_description(const CameraPipelineSpec& spec);

// 运行时可用的 GStreamer 版本（链接 gstreamer-1.0）。
std::string gstreamer_runtime_version();

// librga 使用的中间格式名（编译期常量，验证 im2d.h 可用）。
const char* rga_intermediate_format_name();

}  // namespace media
}  // namespace railhmi
