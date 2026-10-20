#include "video_runtime.h"

#include <QMutexLocker>
#include <QPainter>
#include <QDebug>
#include <algorithm>
#include "railhmi/core/channel_view.h"
#ifdef RAILHMI_HAS_MEDIA
#include "railhmi/media/display_registry.h"
#endif

QImage VideoChannel::image() const {
    QMutexLocker lock(&mutex_);
    return image_;
}

void VideoChannel::setInfo(const QVariantMap& info) {
    if (info_ == info) return;
    info_ = info;
    emit infoChanged();
}

void VideoChannel::setImage(const QImage& image) {
    {
        QMutexLocker lock(&mutex_);
        image_ = image;
    }
    emit frameChanged();
}

VideoFrameItem::VideoFrameItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(false);
}

void VideoFrameItem::setChannel(VideoChannel* channel) {
    if (channel_ == channel) return;
    disconnect(frameConnection_);
    channel_ = channel;
    if (channel_) frameConnection_ = connect(channel_, &VideoChannel::frameChanged,
                                            this, [this]() { update(); });
    update();
    emit channelChanged();
}

void VideoFrameItem::paint(QPainter* painter) {
    const QImage image = channel_ ? channel_->image() : QImage();
    if (image.isNull()) return;
    const QSizeF size = QSizeF(image.size()).scaled(boundingRect().size(), Qt::KeepAspectRatio);
    const QRectF target((width() - size.width()) / 2, (height() - size.height()) / 2,
                        size.width(), size.height());
    painter->drawImage(target, image);
}

struct VideoRuntime::Impl {
    railhmi::core::AppSettings settings;
    std::vector<railhmi::core::CameraSlot> registry;
    std::vector<VideoChannel*> channels;
    std::vector<QString> errors;
    std::vector<unsigned long long> generations, sequences;
    std::vector<long long> next_frame_ms, image_capture_ms;
    bool running = false;
#ifdef RAILHMI_HAS_MEDIA
    std::unique_ptr<railhmi::media::DisplayRegistry> display;
#endif
};

VideoRuntime::VideoRuntime(const railhmi::core::AppSettings& settings, QObject* parent)
    : QObject(parent), impl_(new Impl) {
    impl_->settings = settings;
    impl_->registry = railhmi::core::build_registry(settings);
    for (std::size_t i = 0; i < impl_->registry.size(); ++i)
        impl_->channels.push_back(new VideoChannel(this));
    impl_->errors.resize(impl_->channels.size());
    impl_->generations.resize(impl_->channels.size());
    impl_->sequences.resize(impl_->channels.size());
    impl_->next_frame_ms.resize(impl_->channels.size());
    impl_->image_capture_ms.resize(impl_->channels.size());
    connect(&timer_, &QTimer::timeout, this, &VideoRuntime::refresh);
    timer_.setInterval(40);
    refresh();
}

VideoRuntime::~VideoRuntime() { stop(); }

QVariantList VideoRuntime::channels() const {
    QVariantList result;
    for (auto* channel : impl_->channels) result.append(QVariant::fromValue(channel));
    return result;
}

void VideoRuntime::start() {
    if (impl_->running) return;
    impl_->running = true;
#ifdef RAILHMI_HAS_MEDIA
    using namespace railhmi;
    // The selected CPU display path uses MPP decode + application-side RGA conversion.
    // GstGL integration is not implemented; never advertise an empty GL surface as live.
    const bool supported = impl_->settings.app.render_path == core::RenderPath::Cpu;
    std::vector<media::ChannelSpec> specs;
    for (std::size_t i = 0; i < impl_->settings.cameras.size(); ++i) {
        const auto& camera = impl_->settings.cameras[i];
        media::ChannelSpec spec;
        spec.camera_id = spec.cfg.camera_id = camera.id;
        spec.cfg.source = impl_->settings.app.simulation ? media::SourceKind::Test : media::SourceKind::Rtsp;
        spec.cfg.url = camera.rtsp_url;
        spec.cfg.username = camera.username;
        spec.cfg.password = qgetenv(camera.credential_env.c_str()).toStdString();
        if (!impl_->settings.app.simulation && spec.cfg.password.empty()) {
            impl_->errors[i] = QString::fromUtf8("缺少摄像头凭据环境变量：")
                              + QString::fromStdString(camera.credential_env);
            spec.start_failure = true;
        }
        if (!supported) {
            impl_->errors[i] = QString::fromUtf8("GL 视频显示尚未接入，请配置 render_path: cpu");
            spec.start_failure = true;
        }
        const auto& t = impl_->settings.thresholds;
        spec.cfg.thresholds = {t.first_frame_timeout_ms, t.freeze_timeout_ms,
                               t.reconnect_threshold, t.reconnect_backoff_ms};
        spec.cfg.ai_queue_depth = impl_->settings.app.ai.queue_depth;
        specs.push_back(std::move(spec));
    }
    impl_->display.reset(new media::DisplayRegistry(std::move(specs), media::RenderPath::Cpu));
    std::string errors;
    const int started = impl_->display->start_all(&errors);
    qInfo() << "video workers started:" << started << "/" << impl_->channels.size();
    for (std::size_t i = 0; i < impl_->channels.size(); ++i) {
        if (!impl_->display->manager().started(i) && impl_->errors[i].isEmpty())
            impl_->errors[i] = QString::fromStdString(impl_->display->manager().start_error(i));
    }
#endif
    refresh();
    timer_.start();
}

void VideoRuntime::stop() {
    timer_.stop();
    impl_->running = false;
#ifdef RAILHMI_HAS_MEDIA
    if (impl_->display) impl_->display->stop_all();
    impl_->display.reset();
#endif
    for (auto* channel : impl_->channels) channel->setImage(QImage());
    refresh();
}

void VideoRuntime::freezeForTest(std::size_t index, bool freeze) {
#ifdef RAILHMI_HAS_MEDIA
    if (!impl_->display) return;
    if (freeze) impl_->display->manager().request_freeze(index);
    else impl_->display->manager().request_resume(index);
#else
    Q_UNUSED(index)
    Q_UNUSED(freeze)
#endif
}

void VideoRuntime::refresh() {
    using namespace railhmi;
    std::vector<core::ChannelRuntime> runtime(impl_->channels.size());
    long long now = 0;
#ifdef RAILHMI_HAS_MEDIA
    now = media::CameraStream::now_ms();
    if (impl_->display) {
        const auto stats = impl_->display->manager().stats();
        for (std::size_t i = 0; i < stats.size(); ++i) {
            const auto& s = stats[i];
            auto& r = runtime[i];
            r.has_stream = true;  // Failed starts are offline, not perpetually "unstarted".
            r.state = s.state;
            r.fps = s.fps;
            r.last_frame_ms = s.last_frame_ms > 0 ? s.last_frame_ms : -1;
            r.reconnect_count = s.reconnect_count;
            r.last_error = s.last_error;
            auto* channel = impl_->channels[i];
            const bool fresh = s.last_frame_ms > 0 && now - s.last_frame_ms <=
                               impl_->settings.thresholds.freeze_timeout_ms;
            const bool ready = s.presentable && fresh;
            if (ready && now >= impl_->next_frame_ms[i]) {
                media::FrameSnapshot frame;
                if (impl_->display->snapshot(impl_->registry[i].id, &frame) && frame.presentable &&
                    frame.generation == s.generation && now - frame.capture_mono_us / 1000 <=
                    impl_->settings.thresholds.freeze_timeout_ms &&
                    (frame.seq != impl_->sequences[i] || frame.generation != impl_->generations[i] ||
                     channel->image().isNull())) {
                    const QImage bgr(frame.data.data(), frame.width, frame.height,
                                     static_cast<int>(frame.stride), QImage::Format_RGB888);
                    channel->setImage(bgr.rgbSwapped()); // Deep copy before snapshot storage goes away.
                    impl_->sequences[i] = frame.seq;
                    impl_->generations[i] = frame.generation;
                    impl_->image_capture_ms[i] = frame.capture_mono_us / 1000;
                    impl_->next_frame_ms[i] = now + std::max(1, 1000 / impl_->settings.cameras[i].display_fps);
                }
            }
            // A single failed RGA conversion need not flash a blank tile. Keep the last
            // good image only inside the same strict freshness window as decoded frames.
            r.presentable = ready && impl_->generations[i] == s.generation &&
                            now - impl_->image_capture_ms[i] <= impl_->settings.thresholds.freeze_timeout_ms &&
                            !channel->image().isNull();
            if (!r.presentable && !channel->image().isNull()) channel->setImage(QImage());
        }
    }
#endif
    const auto views = core::build_channel_views(impl_->registry, runtime, now);
    for (std::size_t i = 0; i < views.size(); ++i) {
        const auto& v = views[i];
        QVariantMap info;
        info["id"] = QString::fromStdString(v.id);
        info["name"] = QString::fromUtf8(v.name.c_str());
        info["position"] = QString::fromStdString(v.position);
        info["badge"] = QString::fromUtf8(v.badge.c_str());
        info["aiEnabled"] = v.ai_enabled;
        info["slot"] = v.slot;
        info["state"] = QString::fromStdString(v.state);
        info["stateLabel"] = QString::fromUtf8(v.state_label.c_str());
        if (v.state == "live" && !v.presentable) info["stateLabel"] = QString::fromUtf8("等待视频帧");
        info["fpsText"] = QString::fromUtf8(v.fps_text.c_str());
        info["lastFrameText"] = QString::fromUtf8(v.last_frame_text.c_str());
        info["reconnectText"] = QString::fromUtf8(v.reconnect_text.c_str());
        info["presentable"] = v.presentable;
        info["placeholder"] = v.placeholder;
        QString detail = impl_->errors[i];
        if (detail.isEmpty()) detail = QString::fromStdString(runtime[i].last_error);
        info["detail"] = detail;
        const auto previous = impl_->channels[i]->info();
        if (previous.value("state") != info["state"] || previous.value("presentable") != info["presentable"])
            qInfo() << "video channel" << info["id"].toString() << info["state"].toString()
                    << "presentable=" << v.presentable;
        impl_->channels[i]->setInfo(info);
    }
}
