#include "replay_decoder.h"

#include <QMutex>
#include <QMutexLocker>
#include <atomic>
#include <chrono>
#include <thread>
#ifdef RAILHMI_HAS_MEDIA
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/sdp/gstsdpmessage.h>
#include <gst/rtsp/gstrtsptransport.h>
#include "railhmi/core/timebase.h"
#include "railhmi/media/frame_store.h"
#include "railhmi/media/rga_convert.h"
#endif

#ifdef RAILHMI_HAS_MEDIA
// This IVR expects control appended after the complete query string (its Content-Base),
// whereas GStreamer 1.20 resolves relative control paths before the query. Make only the
// known IVR media controls absolute, before rtspsrc resolves them. Opt-in per device.
static void legacyHistorySdp(GstElement*, GstSDPMessage* sdp, gpointer data) {
    const auto& location = *static_cast<const QByteArray*>(data);
    for (guint index = 0; index < gst_sdp_message_medias_len(sdp); ++index) {
        auto* media = const_cast<GstSDPMedia*>(gst_sdp_message_get_media(sdp, index));
        for (guint i = 0; i < gst_sdp_media_attributes_len(media); ++i) {
            const auto* attribute = gst_sdp_media_get_attribute(media, i);
            if (g_strcmp0(attribute->key, "control") != 0 ||
                (g_strcmp0(attribute->value, "ctype=video") != 0 &&
                 g_strcmp0(attribute->value, "ctype=audio") != 0)) continue;
            QByteArray control(location); control.append('/'); control.append(attribute->value);
            gst_sdp_media_remove_attribute(media, i);
            gst_sdp_media_add_attribute(media, "control", control.constData());
            break;
        }
    }
}
static gboolean historyVideoStream(GstElement*, guint, GstCaps* caps, gpointer) {
    if (!caps || gst_caps_is_empty(caps)) return FALSE;
    return g_strcmp0(gst_structure_get_string(gst_caps_get_structure(caps, 0), "media"), "video") == 0;
}
#endif

struct ReplayDecoder::Impl {
    QMutex mutex;
    QImage image;
    qint64 position = 0;
    quint64 frame = 0;
    QString state = "stopped", detail;
    std::atomic<bool> running{false}, paused{false};
    std::atomic<double> rate{1.0};
    std::thread worker;
    void status(const QString& value, const QString& message) {
        QMutexLocker lock(&mutex); state = value; detail = message;
        if (value == "error" || value == "stopped") image = QImage();
    }
};

ReplayDecoder::ReplayDecoder(QObject* parent) : QObject(parent), impl_(new Impl) {
    timer_.setInterval(40);
    connect(&timer_, &QTimer::timeout, this, &ReplayDecoder::poll);
}
ReplayDecoder::~ReplayDecoder() { stop(); }
void ReplayDecoder::stop() {
    timer_.stop(); impl_->running = false;
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->status("stopped", "已停止");
    emit frameReady(QImage(), impl_->position);
}
void ReplayDecoder::setPaused(bool paused) { impl_->paused = paused; }
void ReplayDecoder::setRate(double rate) { if (rate >= 0.5 && rate <= 2.0) impl_->rate = rate; }

void ReplayDecoder::start(const QUrl& url, const QString& username, const QString& password,
                           qint64 beginMs, qint64 endMs, bool legacyControl, bool tcpTransport) {
    stop();
    {
        QMutexLocker lock(&impl_->mutex);
        impl_->frame = 0; impl_->position = beginMs; impl_->image = QImage();
    }
    lastFrame_ = 0; lastState_.clear(); lastDetail_.clear();
    impl_->paused = false; impl_->rate = 1.0;
    impl_->status("connecting", "正在打开录像流");
#ifdef RAILHMI_HAS_MEDIA
    impl_->running = true;
    impl_->worker = std::thread([this, url, username, password, beginMs, endMs, legacyControl, tcpTransport]() {
        gst_init(nullptr, nullptr);
        const bool test = url.scheme() == "test";
        const bool file = url.isLocalFile();
        const char* description = test
            ? "videotestsrc num-buffers=100 ! video/x-raw,format=NV12,width=640,height=360,framerate=25/1 ! appsink name=history_frames sync=true max-buffers=2 drop=true"
            : file ? "filesrc name=history_source ! decodebin ! videoconvert ! video/x-raw,format=NV12 ! appsink name=history_frames sync=true max-buffers=2 drop=true"
            : "rtspsrc name=history_source latency=500 timeout=5000000 tcp-timeout=5000000 ! application/x-rtp,media=video ! decodebin ! video/x-raw,format=NV12 ! appsink name=history_frames sync=true max-buffers=2 drop=true";
        GError* error = nullptr;
        GstElement* pipeline = gst_parse_launch(description, &error);
        if (!pipeline || error) {
            if (error) g_error_free(error);
            if (pipeline) gst_object_unref(pipeline);
            impl_->status("error", "录像解码管线创建失败"); impl_->running = false; return;
        }
        GstElement* source = gst_bin_get_by_name(GST_BIN(pipeline), "history_source");
        QByteArray legacyBase;
        if (source) {
            QUrl safeUrl(url); safeUrl.setUserName({}); safeUrl.setPassword({});
            const QByteArray location = file ? url.toLocalFile().toUtf8() : safeUrl.toEncoded();
            g_object_set(source, "location", location.constData(), nullptr);
            if (!file) {
                g_signal_connect(source, "select-stream", G_CALLBACK(historyVideoStream), nullptr);
                g_object_set(source, "user-id", username.toUtf8().constData(),
                    "user-pw", password.toUtf8().constData(),
                    "protocols", tcpTransport ? GST_RTSP_LOWER_TRANS_TCP : GST_RTSP_LOWER_TRANS_UDP, nullptr);
                if (legacyControl) {
                    legacyBase = location;
                    g_signal_connect(source, "on-sdp", G_CALLBACK(legacyHistorySdp), &legacyBase);
                }
            }
            gst_object_unref(source);
        }
        GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "history_frames");
        GstBus* bus = gst_element_get_bus(pipeline);
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        railhmi::media::FrameStore store("history");
        quint64 seq = 0;
        qint64 firstPts = -1, lastSample = railhmi::core::TimeBase::steady_now_us();
        qint64 lastConverted = lastSample;
        bool paused = false, finished = false;
        double rate = 1.0;
        while (impl_->running && !finished) {
            if (paused != impl_->paused.load()) {
                paused = impl_->paused.load();
                gst_element_set_state(pipeline, paused ? GST_STATE_PAUSED : GST_STATE_PLAYING);
                impl_->status(paused ? "paused" : "playing", paused ? "已暂停" : "播放中");
                lastSample = railhmi::core::TimeBase::steady_now_us();
            }
            if (rate != impl_->rate.load()) {
                const double requested = impl_->rate.load();
                gint64 current = 0;
                const bool supported = !test && gst_element_query_position(pipeline, GST_FORMAT_TIME, &current) &&
                    gst_element_seek(pipeline, requested, GST_FORMAT_TIME,
                        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                        GST_SEEK_TYPE_SET, current, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
                if (supported) rate = requested;
                else { impl_->rate = rate; impl_->status(paused ? "paused" : "playing", "设备不支持此倍速，已保持原速"); }
            }
            while (auto* message = gst_bus_pop_filtered(bus,
                static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
                if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                    impl_->status("error", "录像流连接或解码失败，请检查回放权限、地址和网络");
                    finished = true;
                } else { impl_->status("ended", "录像播放结束"); finished = true; }
                gst_message_unref(message);
            }
            if (finished) break;
            if (paused) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue; }
            GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 100 * GST_MSECOND);
            if (!sample) {
                if (railhmi::core::TimeBase::steady_now_us() - lastSample > 10000000) {
                    impl_->status("error", "录像流超过 10 秒没有解码帧"); break;
                }
                continue;
            }
            lastSample = railhmi::core::TimeBase::steady_now_us();
            const auto pts = GST_BUFFER_PTS(gst_sample_get_buffer(sample));
            if (GST_CLOCK_TIME_IS_VALID(pts) && firstPts < 0) firstPts = static_cast<qint64>(pts);
            railhmi::core::FrameMeta meta;
            meta.id.session_id = "history"; meta.id.camera_id = "history";
            meta.id.generation = 1; meta.id.seq = ++seq;
            meta.capture_mono_us = lastSample; meta.capture_wall_ms = -1;
            meta.pts_ns = GST_CLOCK_TIME_IS_VALID(pts) ? static_cast<qint64>(pts) : -1;
            std::string problem;
            const auto published = store.publish(sample, meta, "unknown", "file", &problem);
            gst_sample_unref(sample);
            railhmi::media::BgrImage bgr;
            if (published != railhmi::core::LayoutStatus::Ok ||
                !railhmi::media::nv12_to_bgr(store.latest(), &bgr, &problem)) {
                if (lastSample - lastConverted > 10000000) {
                    impl_->status("error", "录像流超过 10 秒未产生可显示图像"); break;
                }
                continue;
            }
            lastConverted = lastSample;
            const QImage image(bgr.data.data(), bgr.width, bgr.height, bgr.width * 3, QImage::Format_RGB888);
            const qint64 elapsed = firstPts >= 0 && meta.pts_ns >= firstPts ? (meta.pts_ns - firstPts) / 1000000 : 0;
            {
                QMutexLocker lock(&impl_->mutex);
                impl_->image = image.rgbSwapped();
                impl_->position = std::min(endMs, beginMs + elapsed);
                ++impl_->frame;
                impl_->state = "playing"; impl_->detail = "播放中";
            }
        }
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(sink); gst_object_unref(bus); gst_object_unref(pipeline);
        impl_->running = false;
    });
#else
    Q_UNUSED(url) Q_UNUSED(username) Q_UNUSED(password) Q_UNUSED(endMs)
    impl_->status("error", "当前预览构建不支持录像解码，请使用 RK3588 目标构建");
#endif
    timer_.start();
}

void ReplayDecoder::poll() {
    QImage image; qint64 position; quint64 frame; QString state, detail;
    {
        QMutexLocker lock(&impl_->mutex);
        image = impl_->image; position = impl_->position; frame = impl_->frame;
        state = impl_->state; detail = impl_->detail;
    }
    if (frame != lastFrame_ || (state == "error" && lastState_ != state)) {
        lastFrame_ = frame; emit frameReady(image, position);
    }
    if (state != lastState_ || detail != lastDetail_) {
        lastState_ = state; lastDetail_ = detail; emit stateChanged(state, detail);
    }
}
