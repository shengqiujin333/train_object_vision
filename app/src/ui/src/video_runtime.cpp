#include "video_runtime.h"

#include <QMutexLocker>
#include <QPainter>
#include <QDebug>
#include <algorithm>
#include "railhmi/core/channel_view.h"
#include "railhmi/core/recovery_policy.h"
#ifdef RAILHMI_HAS_MEDIA
#include "railhmi/media/display_registry.h"
#endif

namespace {
// 现场六路全是 UDP RTSP，断流常表现为「静默停帧」：rtspsrc 的 timeout=5s 并不保证
// 一定产生 GST_MESSAGE_ERROR（11-13 长跑日志 6h 内只有 4 条 GStreamer CRITICAL，
// 却有 35 次 frozen / 25 次 reconnecting / 4 次 offline）。因此生产链路的自动重连
// 不能只靠 last_error 驱动，必须按「距上一帧时间」逐路独立触发真实 pipeline 重建。
constexpr long long kRebuildNoFrameMs = 8000;    // 大于 rtspsrc timeout_us(5s)，避免抖动误重建
constexpr int kRebuildBaseBackoffMs = 5000;      // 连续失败时的首次退避
constexpr int kRebuildMaxBackoffMs = 60000;      // 退避上限；不设总次数上限，保证无人值守可持续自愈
}

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

void VideoFrameItem::setFillMode(int mode) {
    const int normalized = (mode == 1) ? 1 : 0;
    if (fillMode_ == normalized) return;
    fillMode_ = normalized;
    update();
    emit fillModeChanged();
}

void VideoFrameItem::paint(QPainter* painter) {
    const QImage image = channel_ ? channel_->image() : QImage();
    if (image.isNull()) return;
    // Fit：比例不符时上下/左右留黑边（其它页面保持原行为）。
    // Cover：铺满整个格子，居中裁掉超出部分；格子本身按 16:9 设计时对 16:9 源零裁剪。
    const Qt::AspectRatioMode mode = (fillMode_ == 1)
        ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio;
    const QSizeF size = QSizeF(image.size()).scaled(boundingRect().size(), mode);
    const QRectF target((width() - size.width()) / 2, (height() - size.height()) / 2,
                        size.width(), size.height());
    if (mode == Qt::KeepAspectRatioByExpanding) {
        painter->save();
        painter->setClipRect(boundingRect());
        painter->drawImage(target, image);
        painter->restore();
    } else {
        painter->drawImage(target, image);
    }
}

struct VideoRuntime::Impl {
    railhmi::core::AppSettings settings;
    std::vector<railhmi::core::CameraSlot> registry;
    std::vector<VideoChannel*> channels;
    std::vector<QString> errors;
    std::vector<unsigned long long> generations, sequences;
    std::vector<long long> next_frame_ms, image_capture_ms;
    // 每路独立自动重连看门狗状态（主线程 refresh 推进，worker 线程执行真实重建）。
    std::vector<long long> next_rebuild_ms;       // 下次允许请求重建的时刻（单调 ms）
    std::vector<long long> last_frame_ms_seen;    // 上次观测到的 CameraStream::last_frame_ms
    std::vector<int> rebuild_backoff_ms;          // 当前指数退避
    std::vector<int> rebuild_count;               // 累计重建次数（日志/诊断）
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
    impl_->next_rebuild_ms.assign(impl_->channels.size(), 0);
    impl_->last_frame_ms_seen.assign(impl_->channels.size(), 0);
    impl_->rebuild_backoff_ms.assign(impl_->channels.size(), kRebuildBaseBackoffMs);
    impl_->rebuild_count.assign(impl_->channels.size(), 0);
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

            // ---- 单路独立自动重连看门狗 ----
            // 只有「真实新帧」才刷新截止时间：UDP 静默停帧不带 bus ERROR 也能被发现。
            // 生产链路此前没有任何 request_restart 调用方，状态机到 Offline 后是吸收态，
            // 断流只能靠重启进程恢复。这里在主线程按单调时钟推进，实际重建交给 worker。
            if (s.last_frame_ms > 0 && s.last_frame_ms != impl_->last_frame_ms_seen[i]) {
                impl_->last_frame_ms_seen[i] = s.last_frame_ms;
                impl_->next_rebuild_ms[i] = now + kRebuildNoFrameMs;
                impl_->rebuild_backoff_ms[i] = kRebuildBaseBackoffMs;  // 出帧即复位退避
            }
            if (impl_->next_rebuild_ms[i] <= 0)
                impl_->next_rebuild_ms[i] = now + kRebuildNoFrameMs +
                                            impl_->settings.thresholds.first_frame_timeout_ms;

            if (now >= impl_->next_rebuild_ms[i] && impl_->display->manager().started(i)) {
                const auto kind = core::classify_source_error("gst", s.last_error);
                const bool not_retryable =
                    kind == core::SourceErrorKind::AuthFailed ||
                    kind == core::SourceErrorKind::CredentialMissing ||
                    kind == core::SourceErrorKind::Unsupported ||
                    kind == core::SourceErrorKind::Format;
                if (not_retryable) {
                    // 鉴权/凭据/格式错误按契约 §10 不自动反复重试，只低频复核。
                    impl_->next_rebuild_ms[i] = now + kRebuildMaxBackoffMs;
                } else {
                    ++impl_->rebuild_count[i];
                    const int backoff = impl_->rebuild_backoff_ms[i];
                    qInfo() << "video watchdog: rebuild"
                            << QString::fromStdString(impl_->registry[i].id)
                            << "stalled_ms=" << (s.last_frame_ms > 0 ? now - s.last_frame_ms : -1)
                            << "attempt=" << impl_->rebuild_count[i]
                            << "backoff_ms=" << backoff
                            << "last_error=" << QString::fromStdString(s.last_error);
                    impl_->display->manager().request_restart(i);
                    impl_->rebuild_backoff_ms[i] = std::min(backoff * 2, kRebuildMaxBackoffMs);
                    // 重建后给足首帧宽限：退避随连续失败指数增长，上限 60s，不设总次数上限。
                    impl_->next_rebuild_ms[i] = now +
                        std::max<long long>(kRebuildNoFrameMs, impl_->rebuild_backoff_ms[i]);
                }
            }
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
