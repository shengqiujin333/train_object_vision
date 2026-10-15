#include "railhmi/media/camera_stream.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

#include <chrono>
#include <cmath>
#include <sstream>

namespace railhmi {
namespace media {

namespace {

std::string build_pipeline(const CameraStreamConfig& c) {
    std::ostringstream s;
    switch (c.source) {
        case SourceKind::Test:
            s << "videotestsrc is-live=true ! videoconvert "
                 "! video/x-raw,format=NV12,width=1280,height=720,framerate=25/1";
            break;
        case SourceKind::File:
            s << "filesrc location=\"" << c.file << "\" ! decodebin ! video/x-raw,format=NV12";
            break;
        case SourceKind::Rtsp:
            s << "rtspsrc location=\"" << c.url << "\" latency=" << c.latency_ms
              << " timeout=5000000 protocols=udp udp-buffer-size=" << c.udp_buffer_bytes
              << " ! decodebin ! video/x-raw,format=NV12";
            break;
    }
    s << " ! tee name=t "
         "t. ! queue leaky=downstream max-size-buffers=1 ! fakesink name=count_sink sync=false "
         "t. ! queue leaky=downstream max-size-buffers=1 "
         "! appsink name=frame_sink drop=1 max-buffers=1 sync=false";
    return s.str();
}

railhmi::core::FrameStats frame_stats(const GstVideoFrame& vf) {
    railhmi::core::FrameStats fs;
    const unsigned char* y = static_cast<const unsigned char*>(vf.data[0]);
    const int stride = vf.info.stride[0];
    const int w = vf.info.width;
    const int h = vf.info.height;
    long long sum = 0, sum2 = 0, n = 0;
    for (int r = 0; r < h; r += 2) {
        const unsigned char* row = y + static_cast<size_t>(r) * stride;
        for (int c = 0; c < w; c += 2) {
            const long long v = row[c];
            sum += v;
            sum2 += v * v;
            ++n;
        }
    }
    if (n > 0) {
        const double mean = static_cast<double>(sum) / n;
        double var = static_cast<double>(sum2) / n - mean * mean;
        if (var < 0) var = 0;
        fs.mean_luma = mean;
        fs.stddev_luma = std::sqrt(var);
        fs.lap_var = -1.0;  // 无拉普拉斯方差；不判定模糊
    }
    return fs;
}

}  // namespace

struct CameraStream::Impl {
    CameraStreamConfig cfg;
    GstElement* pipeline = nullptr;
    GstElement* frame_sink = nullptr;
    GstElement* count_sink = nullptr;
    GstBus* bus = nullptr;
    railhmi::core::StreamStateMachine* machine = nullptr;
    std::deque<long long> frame_times;
    unsigned long long frames_source = 0;
    long long last_frame_ms = 0;
    bool cut = false;
    bool started = false;
    std::string last_error;

    ~Impl() { teardown(); }

    void teardown() {
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
        }
        if (bus) { gst_object_unref(bus); bus = nullptr; }
        if (count_sink) { gst_object_unref(count_sink); count_sink = nullptr; }
        if (frame_sink) { gst_object_unref(frame_sink); frame_sink = nullptr; }
        if (pipeline) { gst_object_unref(pipeline); pipeline = nullptr; }
        delete machine;
        machine = nullptr;
        started = false;
    }
};

namespace {
GstPadProbeReturn count_probe(GstPad*, GstPadProbeInfo*, gpointer data) {
    static_cast<unsigned long long*>(data)[0]++;
    return GST_PAD_PROBE_OK;
}
}  // namespace

CameraStream::CameraStream(CameraStreamConfig cfg) : impl_(new Impl()) {
    impl_->cfg = std::move(cfg);
}

CameraStream::~CameraStream() {
    delete impl_;
}

long long CameraStream::now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool CameraStream::start(std::string* err) {
    if (impl_->started) return true;
    gst_init(nullptr, nullptr);
    impl_->machine = new railhmi::core::StreamStateMachine(impl_->cfg.camera_id, impl_->cfg.thresholds);

    const std::string desc = build_pipeline(impl_->cfg);
    GError* gerr = nullptr;
    impl_->pipeline = gst_parse_launch(desc.c_str(), &gerr);
    if (gerr) {
        impl_->last_error = gerr->message;
        if (err) *err = gerr->message;
        g_error_free(gerr);
    }
    if (!impl_->pipeline) {
        if (err && err->empty()) *err = "pipeline build failed";
        return false;
    }
    impl_->count_sink = gst_bin_get_by_name(GST_BIN(impl_->pipeline), "count_sink");
    if (impl_->count_sink) {
        GstPad* pad = gst_element_get_static_pad(impl_->count_sink, "sink");
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, count_probe, &impl_->frames_source, nullptr);
        gst_object_unref(pad);
    }
    impl_->frame_sink = gst_bin_get_by_name(GST_BIN(impl_->pipeline), "frame_sink");
    impl_->bus = gst_element_get_bus(impl_->pipeline);

    impl_->machine->start(now_ms());
    gst_element_set_state(impl_->pipeline, GST_STATE_PLAYING);
    impl_->started = true;
    return true;
}

void CameraStream::stop() {
    impl_->teardown();
}

void CameraStream::simulate_cut() {
    if (!impl_) return;
    impl_->cut = true;
    impl_->last_error = "simulated stream cut (FI-01)";
    if (impl_->machine) impl_->machine->on_source_error(now_ms(), impl_->last_error);
    if (impl_->pipeline) gst_element_set_state(impl_->pipeline, GST_STATE_NULL);
}

bool CameraStream::pump(int timeout_ms) {
    if (!impl_ || !impl_->started) return false;
    const long long now = now_ms();
    bool got = false;

    if (impl_->bus) {
        GstMessage* msg = nullptr;
        while ((msg = gst_bus_pop_filtered(
                    impl_->bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) != nullptr) {
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                GError* e = nullptr;
                gchar* dbg = nullptr;
                gst_message_parse_error(msg, &e, &dbg);
                impl_->last_error = e ? e->message : "bus error";
                if (impl_->machine) impl_->machine->on_source_error(now, impl_->last_error);
                if (e) g_error_free(e);
                g_free(dbg);
            } else {
                impl_->last_error = "eos";
                if (impl_->machine) impl_->machine->on_source_error(now, "eos");
            }
            gst_message_unref(msg);
        }
    }

    if (!impl_->cut && impl_->frame_sink) {
        GstSample* s = gst_app_sink_try_pull_sample(GST_APP_SINK(impl_->frame_sink),
                                                    static_cast<GstClockTime>(timeout_ms) * GST_MSECOND);
        if (s) {
            railhmi::core::QualityFlags q;
            GstCaps* caps = gst_sample_get_caps(s);
            GstBuffer* buf = gst_sample_get_buffer(s);
            GstVideoInfo info;
            GstVideoFrame vf;
            if (gst_video_info_from_caps(&info, caps) && gst_video_frame_map(&vf, &info, buf, GST_MAP_READ)) {
                q = railhmi::core::classify_quality(frame_stats(vf), impl_->cfg.quality);
                gst_video_frame_unmap(&vf);
            }
            if (impl_->machine) impl_->machine->on_frame(now, q);
            impl_->last_frame_ms = now;
            impl_->frame_times.push_back(now);
            got = true;
            gst_sample_unref(s);
        }
    }

    if (impl_->machine) impl_->machine->tick(now);
    while (!impl_->frame_times.empty() && now - impl_->frame_times.front() > 2000) {
        impl_->frame_times.pop_front();
    }
    return got;
}

StreamStats CameraStream::stats() const {
    StreamStats st;
    if (!impl_ || !impl_->machine) return st;
    const long long now = now_ms();
    st.state = impl_->machine->state();
    st.reconnect_count = impl_->machine->reconnect_count();
    st.last_frame_ms = impl_->last_frame_ms;
    st.presentable = impl_->machine->may_present_as_live(now);
    st.fps = impl_->frame_times.size() / 2.0;  // 2s 滑窗
    st.last_error = impl_->last_error;
    return st;
}

}  // namespace media
}  // namespace railhmi
