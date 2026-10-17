#include "railhmi/media/camera_stream.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <sstream>
#include <thread>

namespace railhmi {
namespace media {

namespace {

const char* origin_of(SourceKind k) {
    switch (k) {
        case SourceKind::Rtsp: return "live";
        case SourceKind::File: return "file";
        case SourceKind::Test: return "simulation";
    }
    return "live";
}

std::string build_pipeline(const CameraStreamConfig& c) {
    CameraPipelineSpec spec;
    switch (c.source) {
        case SourceKind::Test: spec.source = PipelineSource::Test; break;
        case SourceKind::File: spec.source = PipelineSource::File; break;
        case SourceKind::Rtsp: spec.source = PipelineSource::Rtsp; break;
    }
    spec.camera_id = c.camera_id;
    spec.url = c.url;
    spec.username = c.username;
    spec.password = c.password;
    spec.latency_ms = c.latency_ms;
    spec.timeout_us = c.timeout_us;
    spec.udp_buffer_bytes = c.udp_buffer_bytes;
    spec.decode = c.decode;
    spec.file = c.file;
    spec.test_is_live = c.test_is_live;
    spec.test_width = c.test_width;
    spec.test_height = c.test_height;
    spec.test_fps = c.test_fps;
    spec.path = RenderPath::Cpu;  // 单路探针固定 appsink（cpu）；显示路径由上层选择
    return build_pipeline_description(spec);
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
    bool frozen = false;
    bool started = false;
    long long bus_state_changes = 0;
    railhmi::core::QualityFlags last_quality;
    std::string last_error;
    std::string redact_src;   // 用于错误日志脱敏的凭据（仅内存）
    std::unique_ptr<railhmi::core::FrameSequencer> sequencer;
    railhmi::core::FrameMeta last_meta;
    long long frames_emitted = 0;
    std::unique_ptr<FrameStore> store;
    std::unique_ptr<FrameDispatcher> dispatcher;
    BgrImage last_bgr;
    long long bgr_converted = 0;
    long long bgr_failed = 0;
    std::string bgr_error;

    std::string scrub(const std::string& t) const { return redact_secret(t, redact_src); }

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
    impl_->store.reset(new FrameStore(impl_->cfg.camera_id));
    DispatcherConfig dc;
    dc.display_capacity = 1;
    dc.ai_capacity = impl_->cfg.ai_queue_depth > 0 ? static_cast<std::size_t>(impl_->cfg.ai_queue_depth) : 1;
    dc.recording_capacity = impl_->cfg.recording_queue_frames ? impl_->cfg.recording_queue_frames : 1;
    dc.recording_bytes_capacity = impl_->cfg.recording_queue_bytes;
    impl_->dispatcher.reset(new FrameDispatcher(dc));
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

    // 新会话：清除上次断流/冻结标志与统计，避免重建后不再拉帧或忙等。
    impl_->cut = false;
    impl_->frozen = false;
    impl_->last_error.clear();
    impl_->frame_times.clear();
    impl_->last_frame_ms = 0;
    impl_->last_quality = railhmi::core::QualityFlags{};

    // 帧身份：session 跨相机共享；每次真正重建（本次 start）generation+1、seq 归零。
    if (!impl_->sequencer) {
        const std::string sid = impl_->cfg.session_id.empty() ? railhmi::core::make_session_id()
                                                              : impl_->cfg.session_id;
        impl_->sequencer.reset(new railhmi::core::FrameSequencer(sid, impl_->cfg.camera_id));
    }
    impl_->sequencer->begin_generation();
    impl_->last_meta = railhmi::core::FrameMeta{};
    impl_->last_meta.id.session_id = impl_->sequencer->session_id();
    impl_->last_meta.id.camera_id = impl_->cfg.camera_id;
    impl_->last_meta.id.generation = impl_->sequencer->generation();

    // 解析用于日志脱敏的凭据：优先显式 password，否则从 rtsp URL 拆出。
    impl_->redact_src = impl_->cfg.password;
    if (impl_->redact_src.empty() && impl_->cfg.source == SourceKind::Rtsp) {
        std::string loc, u, p;
        if (split_rtsp_userinfo(impl_->cfg.url, &loc, &u, &p)) impl_->redact_src = p;
    }

    const std::string desc = build_pipeline(impl_->cfg);
    GError* gerr = nullptr;
    impl_->pipeline = gst_parse_launch(desc.c_str(), &gerr);
    if (gerr) {
        impl_->last_error = impl_->scrub(gerr->message);
        if (err) *err = impl_->scrub(gerr->message);
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

void CameraStream::simulate_freeze() {
    if (!impl_) return;
    impl_->frozen = true;
    impl_->last_error = "simulated stream freeze (FI-02)";
    if (impl_->pipeline) gst_element_set_state(impl_->pipeline, GST_STATE_PAUSED);
}

void CameraStream::simulate_resume() {
    if (!impl_) return;
    impl_->frozen = false;
    impl_->last_error.clear();
    if (impl_->pipeline) gst_element_set_state(impl_->pipeline, GST_STATE_PLAYING);
}

bool CameraStream::pump(int timeout_ms) {
    if (!impl_ || !impl_->started) return false;
    const long long now = now_ms();
    bool got = false;

    if (impl_->bus) {
        GstMessage* msg = nullptr;
        while ((msg = gst_bus_pop_filtered(
                    impl_->bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS |
                                                           GST_MESSAGE_STATE_CHANGED))) != nullptr) {
            const GstMessageType mt = GST_MESSAGE_TYPE(msg);
            if (mt == GST_MESSAGE_STATE_CHANGED) {
                ++impl_->bus_state_changes;
            } else if (mt == GST_MESSAGE_ERROR) {
                GError* e = nullptr;
                gchar* dbg = nullptr;
                gst_message_parse_error(msg, &e, &dbg);
                impl_->last_error = impl_->scrub(e ? e->message : "bus error");
                if (impl_->machine) impl_->machine->on_source_error(now, impl_->last_error);
                if (e) g_error_free(e);
                g_free(dbg);
            } else if (impl_->cfg.source == SourceKind::File) {
                // 文件源循环播放（ENV-C：录制文件循环模拟流）；EOS 不算源错误。
                gst_element_seek_simple(
                    impl_->pipeline, GST_FORMAT_TIME,
                    static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), 0);
            } else {
                impl_->last_error = "eos";
                if (impl_->machine) impl_->machine->on_source_error(now, "eos");
            }
            gst_message_unref(msg);
        }
    }

    if (!impl_->cut && !impl_->frozen && impl_->frame_sink) {
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
            // 帧身份 + 时间（单调接收时刻；UTC 需已校准；PTS 无效则 -1）。
            long long pts_ns = -1;
            if (buf && GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(buf)))
                pts_ns = static_cast<long long>(GST_BUFFER_PTS(buf));
            const long long mono_us = railhmi::core::TimeBase::steady_now_us();
            long long wall_ms = -1;
            if (impl_->cfg.timebase && impl_->cfg.timebase->calibrated())
                wall_ms = impl_->cfg.timebase->to_wall_ms(mono_us);
            if (impl_->sequencer)
                impl_->last_meta = impl_->sequencer->next(mono_us, wall_ms, pts_ns);
            ++impl_->frames_emitted;
            if (impl_->store) {
                std::string why;
                impl_->store->publish(s, impl_->last_meta, "", origin_of(impl_->cfg.source), &why);
            }
            // 三类消费者有界分发（显示 latest-only / 推理丢旧 / 录像独立有界）；非阻塞。
            if (impl_->dispatcher && impl_->store) {
                impl_->dispatcher->dispatch(impl_->store->latest());
            }
            // 取流 worker 内 NV12→BGR（librga BT601_FULL）；失败只记错误，不丢帧。
            if (impl_->cfg.convert_bgr && impl_->store) {
                FrameLease lease = impl_->store->latest();
                BgrImage img;
                std::string cerr;
                if (nv12_to_bgr(lease, &img, &cerr)) {
                    impl_->last_bgr = std::move(img);
                    ++impl_->bgr_converted;
                    impl_->bgr_error.clear();
                } else {
                    ++impl_->bgr_failed;
                    impl_->bgr_error = impl_->scrub(cerr);
                }
            }
            if (impl_->machine) impl_->machine->on_frame(now, q);
            impl_->last_quality = q;
            impl_->last_frame_ms = now;
            impl_->frame_times.push_back(now);
            got = true;
            gst_sample_unref(s);
        }
    } else if (impl_->cut || impl_->frozen) {
        // 断流/冻结时不做 pull，按有限时长休眠避免 worker 忙等（保持状态机 tick 节拍）。
        const int wait_ms = timeout_ms > 0 ? timeout_ms : 200;
        std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
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
    st.session_id = impl_->last_meta.id.session_id;
    st.generation = impl_->last_meta.id.generation;
    st.last_seq = impl_->last_meta.id.seq;
    st.capture_wall_ms = impl_->last_meta.capture_wall_ms;
    st.pts_ns = impl_->last_meta.pts_ns;
    st.quality_flags = impl_->last_quality.describe();
    st.bus_state_changes = impl_->bus_state_changes;
    if (impl_->dispatcher) {
        st.display_dropped = impl_->dispatcher->display_dropped();
        st.ai_dropped = impl_->dispatcher->ai_dropped();
        st.recording_overflow = impl_->dispatcher->recording_overflow();
        st.ai_depth = impl_->dispatcher->ai_depth();
        st.recording_depth = impl_->dispatcher->recording_depth();
    }
    return st;
}

std::vector<railhmi::core::StreamEvent> CameraStream::drain_events() {
    if (!impl_ || !impl_->machine) return {};
    return impl_->machine->drain_events();
}

bool CameraStream::presentable_now() const {
    if (!impl_ || !impl_->machine) return false;
    const long long now = now_ms();
    if (!impl_->machine->may_present_as_live(now)) return false;
    if (!impl_->store) return false;
    FrameLease lease = impl_->store->latest();
    if (!lease.valid()) return false;
    return lease.descriptor().generation == generation();
}

bool CameraStream::lease_presentable(const FrameLease& lease) const {
    if (!impl_ || !impl_->machine) return false;
    if (!lease.valid()) return false;
    if (lease.descriptor().generation != generation()) return false;
    return impl_->machine->may_present_as_live(now_ms());
}

long long CameraStream::bus_state_changes() const {
    return impl_ ? impl_->bus_state_changes : 0;
}

railhmi::core::FrameMeta CameraStream::last_frame_meta() const {
    if (!impl_) return railhmi::core::FrameMeta{};
    return impl_->last_meta;
}

unsigned long long CameraStream::generation() const {
    return (impl_ && impl_->sequencer) ? impl_->sequencer->generation() : 0ULL;
}

long long CameraStream::frames_emitted() const {
    return impl_ ? impl_->frames_emitted : 0;
}

FrameLease CameraStream::latest_frame() const {
    if (!impl_ || !impl_->store) return FrameLease();
    return impl_->store->latest();
}

FrameStore& CameraStream::frame_store() { return *impl_->store; }

FrameDispatcher& CameraStream::dispatcher() { return *impl_->dispatcher; }

const BgrImage& CameraStream::last_bgr() const {
    static const BgrImage kEmpty;
    return impl_ ? impl_->last_bgr : kEmpty;
}

long long CameraStream::bgr_converted() const { return impl_ ? impl_->bgr_converted : 0; }
long long CameraStream::bgr_failed() const { return impl_ ? impl_->bgr_failed : 0; }
const std::string& CameraStream::bgr_last_error() const {
    static const std::string kEmpty;
    return impl_ ? impl_->bgr_error : kEmpty;
}

}  // namespace media
}  // namespace railhmi
