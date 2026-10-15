// railhmi 渲染路径选型 spike (ITEM-002)
//
// 目的: 在目标板 (RK3588, Ubuntu 22.04 aarch64) 上用一路 RTSP 实测两条渲染路径，
//       记录 fps / CPU / GPU / 色彩正确性，供 analyze.py 按判据选定主/回退路径。
//
//   --path gl   : decodebin ! tee -> glupload ! glcolorconvert ! qmlglsink
//                 (QML GstGLVideoItem; 零拷贝/近零拷贝合成候选)
//   --path cpu  : decodebin ! tee -> appsink(NV12) -> librga NV12->BGR
//                 -> QImage -> QQuickPaintedItem (回退路径候选)
//
// 两条路径共用: rtspsrc(强制UDP) ! decodebin ! video/x-raw,format=NV12
// 计数分支 fakesink + pad probe 统计源帧率；CPU/GPU 采样 /proc 与 devfreq。
// 色彩正确性: 经 librga BT601_FULL 转换后检查 BGR 三通道均值是否近似相等
//             (README §2.3 的判据)，并保存一帧快照供人工目视(由用户完成)。
//
// 本文件是 spike 源码；是否能在目标板协商成功属实测结果，本文件不预设结论。

#include <QGuiApplication>
#include <QQuickView>
#include <QQuickItem>
#include <QQuickPaintedItem>
#include <QPainter>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QCommandLineParser>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QFileInfo>
#include <unistd.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <im2d.h>
#include <RgaUtils.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// 统计
// ---------------------------------------------------------------------------
struct ColorCheck {
    bool valid = false;
    double b = 0, g = 0, r = 0;
    double spread = 0;      // max-min of channel means
    bool pass = false;
};

struct Stats {
    std::atomic<unsigned long long> frames_source{0};
    std::atomic<unsigned long long> frames_display{0};
    std::atomic<bool> playing{false};
    std::atomic<bool> ever_playing{false};
    std::mutex mtx;
    std::vector<std::string> errors;
    ColorCheck color;
    std::string snapshot_path;
};

static Stats g_stats;

static void add_error(const std::string &e) {
    std::lock_guard<std::mutex> lk(g_stats.mtx);
    g_stats.errors.push_back(e);
    fprintf(stderr, "[spike] ERROR: %s\n", e.c_str());
}

static GstPadProbeReturn count_probe(GstPad *, GstPadProbeInfo *, gpointer) {
    g_stats.frames_source.fetch_add(1, std::memory_order_relaxed);
    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn display_probe(GstPad *, GstPadProbeInfo *, gpointer) {
    g_stats.frames_display.fetch_add(1, std::memory_order_relaxed);
    return GST_PAD_PROBE_OK;
}

// ---------------------------------------------------------------------------
// librga NV12 -> BGR (BT601_FULL)，判据与 code/main.cc 一致:
// imcvtcolor 的 SUCCESS(1) 与 NOERROR(2) 都算成功。
// ---------------------------------------------------------------------------
static bool nv12_to_bgr_rga(const uint8_t *nv12, int w, int h, int ystride,
                            size_t uv_offset, uint8_t *bgr) {
    int hstride = ystride > 0 ? (int)(uv_offset / (size_t)ystride) : h;
    rga_buffer_t src = wrapbuffer_virtualaddr((void *)nv12, w, h,
                                              RK_FORMAT_YCbCr_420_SP, ystride, hstride);
    rga_buffer_t dst = wrapbuffer_virtualaddr(bgr, w, h, RK_FORMAT_BGR_888, w, h);
    IM_STATUS st = imcvtcolor(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_BGR_888,
                              IM_YUV_TO_RGB_BT601_FULL);
    return st == IM_STATUS_NOERROR || st == IM_STATUS_SUCCESS;
}

static ColorCheck check_color_bgr(const uint8_t *bgr, int w, int h) {
    ColorCheck c;
    long long sb = 0, sg = 0, sr = 0;
    long long n = (long long)w * h;
    if (n <= 0) return c;
    for (long long i = 0; i < n; i++) {
        sb += bgr[i * 3 + 0];
        sg += bgr[i * 3 + 1];
        sr += bgr[i * 3 + 2];
    }
    c.b = (double)sb / n;
    c.g = (double)sg / n;
    c.r = (double)sr / n;
    double mn = c.b < c.g ? c.b : c.g; if (c.r < mn) mn = c.r;
    double mx = c.b > c.g ? c.b : c.g; if (c.r > mx) mx = c.r;
    c.spread = mx - mn;
    c.valid = true;
    c.pass = (c.g > 2.0) && (c.spread < 12.0);   // 绿色伪影/黑白灰检查
    return c;
}

static void save_snapshot(const QImage &img, const std::string &path) {
    if (path.empty() || img.isNull()) return;
    QString p = QString::fromStdString(path);
    QDir().mkpath(QFileInfo(p).absolutePath());
    if (img.save(p, "PNG")) return;
    if (img.save(p, "BMP")) return;
    add_error("snapshot save failed: " + path);
    return;
}

// QImage 从 RGA BGR 缓冲拷贝一份 (回退路径的真实代价之一)
static QImage bgr_to_qimage(const uint8_t *bgr, int w, int h) {
    QImage img((const uchar *)bgr, w, h, w * 3, QImage::Format_BGR888);
    return img.copy();
}

// ---------------------------------------------------------------------------
// CPU 回退路径显示组件: 接收 QImage 并绘制 (QQuickPaintedItem)
// ---------------------------------------------------------------------------
class FrameItem : public QQuickPaintedItem {
    Q_OBJECT
public:
    explicit FrameItem(QQuickItem *parent = nullptr) : QQuickPaintedItem(parent) {}
    void paint(QPainter *p) override {
        if (m_img.isNull()) return;
        p->drawImage(boundingRect(), m_img);
    }
public slots:
    void setImage(const QImage &img) {
        m_img = img;
        g_stats.frames_display.fetch_add(1, std::memory_order_relaxed);
        update();
    }
private:
    QImage m_img;
};

// ---------------------------------------------------------------------------
// /proc、devfreq 采样
// ---------------------------------------------------------------------------
static long long read_cpu_ticks() {
    std::ifstream f("/proc/self/stat");
    std::string line;
    if (!std::getline(f, line)) return -1;
    // comm 可能含空格，取最后一个 ')' 之后
    size_t pos = line.rfind(')');
    if (pos == std::string::npos) return -1;
    std::istringstream ss(line.substr(pos + 2));
    std::vector<long long> v;
    long long x;
    while (ss >> x) v.push_back(x);
    // 以 ')' 后第一个字段为 state(field3); utime=field14 -> idx 14-3=11,
    // stime=field15 -> idx 12
    if (v.size() < 13) return -1;
    return v[11] + v[12];
}

static double read_gpu_load_pct() {
    // Rockchip GPU devfreq: /sys/class/devfreq/*.gpu/load 形如 "12@1000000000"
    QDir d("/sys/class/devfreq");
    for (const QString &name : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!name.contains("gpu", Qt::CaseInsensitive)) continue;
        QFile f(d.filePath(name + "/load"));
        if (!f.open(QIODevice::ReadOnly)) continue;
        QString v = QString::fromUtf8(f.readAll()).trimmed();
        int at = v.indexOf('@');
        bool ok = false;
        double val = (at > 0 ? v.left(at) : v).toDouble(&ok);
        if (ok) return val;
    }
    return -1.0;
}

static double read_rss_mb() {
    std::ifstream f("/proc/self/statm");
    long long size = 0, rss = 0;
    if (!(f >> size >> rss)) return -1.0;
    long page = sysconf(_SC_PAGESIZE);
    return (double)rss * page / (1024.0 * 1024.0);
}

// ---------------------------------------------------------------------------
// 管线
// ---------------------------------------------------------------------------
static std::string build_pipeline(const std::string &path, const std::string &url) {
    std::ostringstream s;
    s << "rtspsrc location=\"" << url
      << "\" latency=500 timeout=5000000 udp-buffer-size=4194304 "
         "! decodebin ! video/x-raw,format=NV12 ! tee name=t "
         "t. ! queue leaky=downstream max-size-buffers=1 ! fakesink name=count_sink sync=false ";
    if (path == "gl") {
        s << "t. ! queue leaky=downstream max-size-buffers=1 "
             "! glupload ! glcolorconvert ! qmlglsink name=videosink ";
        // GL 路径的一次性抓帧+色彩校验分支
        s << "t. ! queue leaky=downstream max-size-buffers=1 "
             "! appsink name=snap_sink drop=1 max-buffers=1 sync=false";
    } else {
        s << "t. ! queue leaky=downstream max-size-buffers=1 "
             "! appsink name=frame_sink drop=1 max-buffers=1 sync=false";
    }
    return s.str();
}

// 从 appsink 拉一帧 NV12 -> RGA -> BGR；返回 false 表示没拿到
static bool pull_nv12_as_bgr(GstElement *sink, std::vector<uint8_t> &bgr,
                             int &w, int &h, ColorCheck &cc) {
    GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 5 * GST_SECOND);
    if (!sample) return false;
    GstCaps *caps = gst_sample_get_caps(sample);
    GstVideoInfo info;
    gst_video_info_from_caps(&info, caps);
    GstBuffer *buf = gst_sample_get_buffer(sample);
    GstVideoFrame vf;
    bool ok = false;
    if (gst_video_frame_map(&vf, &info, buf, GST_MAP_READ)) {
        w = info.width; h = info.height;
        bgr.assign((size_t)w * h * 3, 0);
        size_t uv_off = (size_t)vf.info.offset[1];
        int ystride = vf.info.stride[0];
        ok = nv12_to_bgr_rga((const uint8_t *)vf.data[0], w, h, ystride, uv_off, bgr.data());
        if (ok) cc = check_color_bgr(bgr.data(), w, h);
        gst_video_frame_unmap(&vf);
    }
    gst_sample_unref(sample);
    return ok;
}

// CPU 路径工作线程 (耗时的 RGA 转换与 QImage 拷贝移出 UI 线程)
static void cpu_worker(GstElement *sink, FrameItem *item,
                       const std::string &snapshot_path) {
    std::vector<uint8_t> bgr;
    bool first_done = false;
    while (g_stats.playing.load()) {
        int w = 0, h = 0;
        ColorCheck cc;
        if (!pull_nv12_as_bgr(sink, bgr, w, h, cc)) {
            if (gst_app_sink_is_eos(GST_APP_SINK(sink))) break;
            continue;
        }
        if (!first_done) {
            first_done = true;
            {
                std::lock_guard<std::mutex> lk(g_stats.mtx);
                g_stats.color = cc;
                g_stats.snapshot_path = snapshot_path;
            }
            if (!snapshot_path.empty()) save_snapshot(bgr_to_qimage(bgr.data(), w, h), snapshot_path);
        }
        QImage img = bgr_to_qimage(bgr.data(), w, h);
        // 跨线程: 排队投递到 UI 线程的 FrameItem
        QMetaObject::invokeMethod(item, "setImage", Qt::QueuedConnection,
                                  Q_ARG(QImage, img));
    }
}

// GL 路径: 一次性抓帧 + 色彩校验
static void gl_snapshot_once(GstElement *sink, const std::string &snapshot_path) {
    std::vector<uint8_t> bgr;
    int w = 0, h = 0;
    ColorCheck cc;
    if (!pull_nv12_as_bgr(sink, bgr, w, h, cc)) {
        add_error("gl path: snapshot pull failed (no NV12 frame)");
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g_stats.mtx);
        g_stats.color = cc;
        g_stats.snapshot_path = snapshot_path;
    }
    if (!snapshot_path.empty()) save_snapshot(bgr_to_qimage(bgr.data(), w, h), snapshot_path);
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("railhmi-render-spike");

    QCommandLineParser parser;
    parser.setApplicationDescription("railhmi render path spike (gl|cpu)");
    parser.addHelpOption();
    QCommandLineOption optPath("path", "render path: gl|cpu", "path", "gl");
    QCommandLineOption optUrl("url", "RTSP URL", "url");
    QCommandLineOption optDuration("duration", "measurement seconds", "sec", "20");
    QCommandLineOption optOut("out", "write JSON result here", "file");
    QCommandLineOption optSnapshot("snapshot", "save one frame (PNG/BMP) here", "file");
    parser.addOption(optPath); parser.addOption(optUrl);
    parser.addOption(optDuration); parser.addOption(optOut); parser.addOption(optSnapshot);
    parser.process(app);

    const std::string path = parser.value(optPath).toStdString();
    const std::string url = parser.value(optUrl).toStdString();
    const int duration = parser.value(optDuration).toInt();
    const std::string out = parser.value(optOut).toStdString();
    const std::string snapshot = parser.value(optSnapshot).toStdString();

    if (url.empty()) { fprintf(stderr, "--url is required\n"); return 2; }
    if (path != "gl" && path != "cpu") { fprintf(stderr, "--path must be gl|cpu\n"); return 2; }

    gst_init(&argc, &argv);

    if (path == "cpu")
        qmlRegisterType<FrameItem>("RailHmi.Spike", 1, 0, "FrameItem");

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(1280, 720);
    view.setSource(QUrl(path == "gl" ? "qrc:/GlView.qml" : "qrc:/CpuView.qml"));
    if (view.status() != QQuickView::Ready) {
        add_error(std::string("QML load failed: ") +
                  (path == "gl" ? "GlView.qml (GstGL import?)" : "CpuView.qml"));
        // 仍然继续: 结果 JSON 记录失败
    }
    view.show();

    GError *err = nullptr;
    GstElement *pipeline = gst_parse_launch(build_pipeline(path, url).c_str(), &err);
    if (err) { add_error(std::string("pipeline parse: ") + err->message); g_error_free(err); }
    if (!pipeline) { add_error("pipeline build failed"); return 3; }

    GstElement *count_sink = gst_bin_get_by_name(GST_BIN(pipeline), "count_sink");
    if (count_sink) {
        GstPad *pad = gst_element_get_static_pad(count_sink, "sink");
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, count_probe, nullptr, nullptr);
        gst_object_unref(pad);
    }

    FrameItem *frame_item = nullptr;
    if (path == "gl") {
        GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "videosink");
        if (!sink) {
            add_error("qmlglsink not available (gstreamer1.0-qt5 missing?)");
        } else {
            // gl 出图帧率: 计入 videosink 的 sink pad
            GstPad *dp = gst_element_get_static_pad(sink, "sink");
            if (dp) {
                gst_pad_add_probe(dp, GST_PAD_PROBE_TYPE_BUFFER, display_probe, nullptr, nullptr);
                gst_object_unref(dp);
            }
            if (view.rootObject()) {
                QQuickItem *item = view.rootObject()->findChild<QQuickItem *>("videoItem");
                if (!item) add_error("QML 'videoItem' not found");
                else g_object_set(sink, "widget", item, nullptr);
            }
        }
    } else {
        if (view.rootObject())
            frame_item = view.rootObject()->findChild<FrameItem *>("frameItem");
        if (!frame_item) add_error("QML 'frameItem' not found");
    }

    GstBus *bus = gst_element_get_bus(pipeline);
    QTimer bus_timer;
    QObject::connect(&bus_timer, &QTimer::timeout, [&]() {
        GstMessage *msg = nullptr;
        while ((msg = gst_bus_pop_filtered(bus, (GstMessageType)(GST_MESSAGE_ERROR |
                                                                 GST_MESSAGE_EOS))) != nullptr) {
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                GError *e = nullptr; gchar *dbg = nullptr;
                gst_message_parse_error(msg, &e, &dbg);
                add_error(std::string("bus error: ") + (e ? e->message : "?"));
                if (e) g_error_free(e);
                g_free(dbg);
            } else {
                add_error("bus EOS (stream ended)");
            }
            gst_message_unref(msg);
        }
    });
    bus_timer.start(200);

    // 采样
    long long ticks0 = read_cpu_ticks();
    QElapsedTimer wall; wall.start();
    std::vector<double> gpu_samples;
    QTimer gpu_timer;
    QObject::connect(&gpu_timer, &QTimer::timeout, [&]() {
        double l = read_gpu_load_pct();
        if (l >= 0) gpu_samples.push_back(l);
    });
    gpu_timer.start(500);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_stats.playing.store(true);

    std::thread cpu_thread;
    std::thread snap_thread;
    GstElement *frame_sink = gst_bin_get_by_name(GST_BIN(pipeline), "frame_sink");
    GstElement *snap_sink = gst_bin_get_by_name(GST_BIN(pipeline), "snap_sink");
    if (path == "cpu" && frame_sink && frame_item)
        cpu_thread = std::thread(cpu_worker, frame_sink, frame_item, snapshot);
    if (path == "gl" && snap_sink)
        snap_thread = std::thread(gl_snapshot_once, snap_sink, snapshot);

    // 判定是否真的达到 PLAYING
    QTimer state_timer;
    state_timer.setSingleShot(true);
    QObject::connect(&state_timer, &QTimer::timeout, [&]() {
        GstState st = GST_STATE_VOID_PENDING, pend = GST_STATE_VOID_PENDING;
        gst_element_get_state(pipeline, &st, &pend, 0);
        if (st == GST_STATE_PLAYING) g_stats.ever_playing.store(true);
        else
            add_error("pipeline did not reach PLAYING within 8s (negotiation failure)");
    });
    state_timer.start(8000);

    QTimer::singleShot(duration * 1000, [&]() {
        // 收尾
        g_stats.playing.store(false);
        gst_element_set_state(pipeline, GST_STATE_NULL);
        if (cpu_thread.joinable()) cpu_thread.join();
        if (snap_thread.joinable()) snap_thread.join();

        double secs = wall.elapsed() / 1000.0;
        long long ticks1 = read_cpu_ticks();
        double cpu_seconds = -1.0, cpu_pct = -1.0;
        long hz = sysconf(_SC_CLK_TCK);
        if (ticks0 >= 0 && ticks1 >= 0 && hz > 0) {
            cpu_seconds = (double)(ticks1 - ticks0) / hz;
            cpu_pct = secs > 0 ? cpu_seconds / secs * 100.0 : -1.0;
        }
        unsigned long long fs = g_stats.frames_source.load();
        unsigned long long fd = g_stats.frames_display.load();

        QJsonObject o;
        o["path"] = QString::fromStdString(path);
        o["url"] = QString::fromStdString(url);
        o["duration_s"] = secs;
        o["playing"] = g_stats.ever_playing.load();
        o["frames_source"] = (double)fs;
        o["fps_source"] = secs > 0 ? (double)fs / secs : 0.0;
        o["frames_display"] = (double)fd;
        o["fps_display"] = secs > 0 ? (double)fd / secs : 0.0;
        o["cpu_seconds"] = cpu_seconds;
        o["cpu_pct_one_core"] = cpu_pct;
        o["rss_mb"] = read_rss_mb();
        if (!gpu_samples.empty()) {
            double mx = gpu_samples[0], sum = 0;
            for (double v : gpu_samples) { if (v > mx) mx = v; sum += v; }
            o["gpu_load_max"] = mx;
            o["gpu_load_avg"] = sum / gpu_samples.size();
            o["gpu_samples"] = (int)gpu_samples.size();
        } else {
            o["gpu_load_max"] = -1.0;
            o["gpu_load_avg"] = -1.0;
            o["gpu_samples"] = 0;
        }
        ColorCheck cc;
        std::vector<std::string> errs;
        std::string snap;
        {
            std::lock_guard<std::mutex> lk(g_stats.mtx);
            cc = g_stats.color; errs = g_stats.errors; snap = g_stats.snapshot_path;
        }
        QJsonObject c;
        c["valid"] = cc.valid;
        c["b_mean"] = cc.b; c["g_mean"] = cc.g; c["r_mean"] = cc.r;
        c["spread"] = cc.spread; c["pass"] = cc.pass;
        o["color"] = c;
        o["snapshot"] = QString::fromStdString(snap);
        QJsonArray ea;
        for (const auto &e : errs) ea.append(QString::fromStdString(e));
        o["errors"] = ea;
        o["negotiated"] = (fs > 0);

        QJsonDocument doc(o);
        QByteArray bytes = doc.toJson(QJsonDocument::Indented);
        if (!out.empty()) {
            QFile f(QString::fromStdString(out));
            QDir().mkpath(QFileInfo(f).absolutePath());
            if (f.open(QIODevice::WriteOnly)) f.write(bytes);
            else fprintf(stderr, "cannot write %s\n", out.c_str());
        }
        fwrite(bytes.constData(), 1, bytes.size(), stdout);
        fflush(stdout);
        QCoreApplication::quit();
    });

    return app.exec();
}

#include "main.moc"
