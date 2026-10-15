#include "railhmi/media/pipeline_factory.h"

#include <gst/gst.h>
#include <im2d.h>

#include <sstream>

namespace railhmi {
namespace media {

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

std::string build_pipeline_description(const CameraPipelineSpec& spec) {
    std::ostringstream s;
    s << "rtspsrc location=\"" << spec.url << "\" latency=" << spec.latency_ms
      << " timeout=5000000 protocols=udp udp-buffer-size=" << spec.udp_buffer_bytes
      << " ! decodebin ! video/x-raw,format=NV12 ! tee name=t "
      << "t. ! queue leaky=downstream max-size-buffers=1 ! fakesink sync=false ";
    if (spec.path == RenderPath::Gl) {
        s << "t. ! queue leaky=downstream max-size-buffers=1 "
             "! glupload ! glcolorconvert ! qmlglsink name=videosink";
    } else {
        s << "t. ! queue leaky=downstream max-size-buffers=1 "
             "! appsink name=frame_sink drop=1 max-buffers=1 sync=false";
    }
    return s.str();
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
