#include "railhmi/media/pipeline_factory.h"

#include <gst/gst.h>
#include <im2d.h>

#include <sstream>

namespace railhmi {
namespace media {

namespace {

// gst-parse 字符串中的双引号/反斜杠转义（URL/路径/凭据可能含特殊字符）。
std::string gst_quote(const std::string& v) {
    std::string o;
    o.reserve(v.size() + 2);
    for (const char c : v) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

}  // namespace

RenderPath render_path_from_string(const std::string& s, bool* ok) {
    if (ok) *ok = true;
    if (s == "gl") return RenderPath::Gl;
    if (s == "cpu") return RenderPath::Cpu;
    if (ok) *ok = false;
    return RenderPath::Cpu;
}

const char* render_path_name(RenderPath p) {
    return p == RenderPath::Gl ? "gl" : "cpu";
}

const char* pipeline_source_name(PipelineSource s) {
    switch (s) {
        case PipelineSource::Test: return "test";
        case PipelineSource::File: return "file";
        case PipelineSource::Rtsp: return "rtsp";
    }
    return "?";
}

bool parse_pipeline_source(const std::string& s, PipelineSource* out) {
    if (s == "test") { *out = PipelineSource::Test; return true; }
    if (s == "file") { *out = PipelineSource::File; return true; }
    if (s == "rtsp") { *out = PipelineSource::Rtsp; return true; }
    return false;
}

const char* decode_mode_name(DecodeMode m) {
    switch (m) {
        case DecodeMode::Auto: return "auto";
        case DecodeMode::ExplicitH264: return "h264";
        case DecodeMode::ExplicitH265: return "h265";
    }
    return "?";
}

bool parse_decode_mode(const std::string& s, DecodeMode* out) {
    if (s == "auto" || s == "decodebin") { *out = DecodeMode::Auto; return true; }
    if (s == "h264" || s == "h.264") { *out = DecodeMode::ExplicitH264; return true; }
    if (s == "h265" || s == "h.265" || s == "hevc") { *out = DecodeMode::ExplicitH265; return true; }
    return false;
}

bool split_rtsp_userinfo(const std::string& url, std::string* location,
                         std::string* username, std::string* password) {
    const std::size_t scheme = url.find("://");
    const std::size_t start = (scheme == std::string::npos) ? 0 : scheme + 3;
    const std::size_t at = url.find('@', start);
    const std::size_t slash = url.find('/', start);
    if (at == std::string::npos || at == start) return false;
    if (slash != std::string::npos && at > slash) return false;  // '@' 在路径中，非用户信息
    const std::string userinfo = url.substr(start, at - start);
    const std::size_t colon = userinfo.find(':');
    if (location) *location = url.substr(0, start) + url.substr(at + 1);
    if (username) *username = (colon == std::string::npos) ? userinfo : userinfo.substr(0, colon);
    if (password) *password = (colon == std::string::npos) ? "" : userinfo.substr(colon + 1);
    return true;
}

std::string build_nv12_prefix(const CameraPipelineSpec& spec) {
    std::ostringstream s;
    switch (spec.source) {
        case PipelineSource::Test: {
            s << "videotestsrc is-live=" << (spec.test_is_live ? "true" : "false")
              << " ! videoconvert ! video/x-raw,format=NV12"
              << ",width=" << spec.test_width << ",height=" << spec.test_height
              << ",framerate=" << spec.test_fps << "/1";
            return s.str();
        }
        case PipelineSource::File: {
            s << "filesrc location=\"" << gst_quote(spec.file) << "\""
              << " ! decodebin ! video/x-raw,format=NV12";
            return s.str();
        }
        case PipelineSource::Rtsp: {
            std::string loc = spec.url;
            std::string user = spec.username;
            std::string pass = spec.password;
            if (user.empty() && pass.empty()) {
                std::string u, p;
                if (split_rtsp_userinfo(spec.url, &loc, &u, &p)) {
                    user = u;
                    pass = p;
                }
            }
            s << "rtspsrc location=\"" << gst_quote(loc) << "\"";
            // 认证经 rtspsrc 属性传入，不拼接进 URL。
            if (!user.empty()) s << " user-id=\"" << gst_quote(user) << "\"";
            if (!pass.empty()) s << " user-pw=\"" << gst_quote(pass) << "\"";
            s << " latency=" << spec.latency_ms << " timeout=" << spec.timeout_us
              << " protocols=udp udp-buffer-size=" << spec.udp_buffer_bytes;
            // 强制 UDP；无 protocols=tcp 回退路径。
            // 只选视频 RTP pad：RTP 层先过滤 media=video，音频/数据 pad 不进入解码链。
            s << " ! application/x-rtp,media=video";
            switch (spec.decode) {
                case DecodeMode::Auto:
                    // decodebin 自适应 H.264/H.265；输入已是纯视频 RTP。
                    s << " ! decodebin";
                    break;
                case DecodeMode::ExplicitH265:
                    s << " ! rtph265depay ! h265parse ! mppvideodec";
                    break;
                case DecodeMode::ExplicitH264:
                    s << " ! rtph264depay ! h264parse ! mppvideodec";
                    break;
            }
            s << " ! video/x-raw,format=NV12";
            return s.str();
        }
    }
    return "";
}

std::string build_pipeline_description(const CameraPipelineSpec& spec) {
    // 显示/计数分支保持非阻塞（leaky）；file 源循环播放作为解码路径源，非实时时钟基准。
    std::ostringstream s;
    s << build_nv12_prefix(spec)
      << " ! tee name=t "
         "t. ! queue leaky=downstream max-size-buffers=1 ! fakesink name=count_sink sync=false ";
    if (spec.path == RenderPath::Gl) {
        s << "t. ! queue leaky=downstream max-size-buffers=1 "
             "! glupload ! glcolorconvert ! qmlglsink name=videosink";
    } else {
        s << "t. ! queue leaky=downstream max-size-buffers=1 "
             "! appsink name=frame_sink drop=1 max-buffers=1 sync=false";
    }
    return s.str();
}

std::string redact_secret(const std::string& text, const std::string& secret) {
    if (secret.empty()) return text;
    std::string out = text;
    std::size_t pos = 0;
    while ((pos = out.find(secret, pos)) != std::string::npos) {
        out.replace(pos, secret.size(), "***");
        pos += 3;
    }
    return out;
}

std::string gstreamer_runtime_version() {
    guint major = 0, minor = 0, micro = 0, nano = 0;
    gst_version(&major, &minor, &micro, &nano);
    std::ostringstream s;
    s << major << "." << minor << "." << micro;
    return s.str();
}

const char* rga_intermediate_format_name() {
    // 与设计一致：NV12 源，BGR 目标。
    return "RK_FORMAT_YCbCr_420_SP -> RK_FORMAT_BGR_888";
}

}  // namespace media
}  // namespace railhmi
