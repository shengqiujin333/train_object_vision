/*
 * railhmi 渲染路径 spike — 无 Qt 的核心管线测量 (ITEM-002 辅助)
 *
 * 目的: 当目标板缺少 Qt Quick/QML 开发包(无法构建 app/spikes/render_path 的 QML spike)时,
 *       仍能对 CPU 回退路径的**核心管线**做真实测量:
 *         输入源 -> NV12 -> appsink -> librga NV12->BGR(BT601_FULL) -> 通道均值/色彩判据
 *       记录 fps_source / fps_display(转换帧率) / CPU% / GPU(devfreq, 若存在) / BGR 三通道均值。
 *
 * 边界说明(不得混淆): 本测量不含 Qt 绘制/纹理上传, 因此只证明“解码+RGA 转换”核心可用;
 *   QML 端的 QQuickPaintedItem 出图代价未包含。结果为替代测量, 在文档中明确标注。
 *
 * 构建(目标板, aarch64 本地):
 *   gcc -O2 -o spike_core spike_core.c $(pkg-config --cflags --libs \
 *       gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) -lrga
 *
 * 运行:
 *   ./spike_core --source test --duration 8 --out core.json [--snapshot core.bmp]
 *   ./spike_core --source file --file clip.mp4 ...
 *   ./spike_core --source rtsp --url 'rtsp://user:pass@host/...' ...   # 需项目授权
 */

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <im2d.h>
#include <RgaUtils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/time.h>

static unsigned long long g_frames_source = 0;

static GstPadProbeReturn count_probe(GstPad *p, GstPadProbeInfo *i, gpointer d) {
    (void)p; (void)i; (void)d;
    g_frames_source++;
    return GST_PAD_PROBE_OK;
}

static int64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static long long read_cpu_ticks(void) {
    FILE *f = fopen("/proc/self/stat", "r");
    if (!f) return -1;
    char buf[4096];
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return -1; }
    fclose(f);
    char *rp = strrchr(buf, ')');
    if (!rp) return -1;
    long long fields[64];
    int n = 0;
    char *tok = strtok(rp + 2, " ");
    while (tok && n < 64) { fields[n++] = atoll(tok); tok = strtok(NULL, " "); }
    if (n < 13) return -1;
    return fields[11] + fields[12];   /* utime + stime */
}

static double read_rss_mb(void) {
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f) return -1.0;
    long long size = 0, rss = 0;
    if (fscanf(f, "%lld %lld", &size, &rss) != 2) { fclose(f); return -1.0; }
    fclose(f);
    return (double)rss * (double)sysconf(_SC_PAGESIZE) / (1024.0 * 1024.0);
}

static double read_gpu_load_pct(void) {
    DIR *d = opendir("/sys/class/devfreq");
    if (!d) return -1.0;
    struct dirent *e;
    double val = -1.0;
    while ((e = readdir(d))) {
        if (strstr(e->d_name, "gpu") == NULL) continue;
        char path[512];
        snprintf(path, sizeof(path), "/sys/class/devfreq/%s/load", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char buf[128] = {0};
        if (fgets(buf, sizeof(buf), f)) {
            char *at = strchr(buf, '@');
            if (at) *at = '\0';
            val = atof(buf);
        }
        fclose(f);
        if (val >= 0) break;
    }
    closedir(d);
    return val;
}

/* BMP (24-bit BGR, 自底向上) — 规避板端 OpenCV/JPEG 编码的历史冲突 */
static int write_bmp(const char *path, const uint8_t *bgr, int w, int h) {
    int row = w * 3;
    int pad = (4 - (row % 4)) % 4;
    int stride = row + pad;
    uint32_t filesize = 54 + (uint32_t)stride * h;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = filesize & 0xff; hdr[3] = (filesize >> 8) & 0xff;
    hdr[4] = (filesize >> 16) & 0xff; hdr[5] = (filesize >> 24) & 0xff;
    hdr[10] = 54; hdr[14] = 40;
    hdr[18] = w & 0xff; hdr[19] = (w >> 8) & 0xff;
    hdr[20] = (w >> 16) & 0xff; hdr[21] = (w >> 24) & 0xff;
    hdr[22] = h & 0xff; hdr[23] = (h >> 8) & 0xff;
    hdr[24] = (h >> 16) & 0xff; hdr[25] = (h >> 24) & 0xff;
    hdr[26] = 1; hdr[28] = 24;
    hdr[34] = (uint32_t)(stride * h) & 0xff;
    hdr[35] = ((uint32_t)(stride * h) >> 8) & 0xff;
    hdr[36] = ((uint32_t)(stride * h) >> 16) & 0xff;
    hdr[37] = ((uint32_t)(stride * h) >> 24) & 0xff;
    fwrite(hdr, 1, 54, f);
    uint8_t *padrow = (uint8_t *)calloc(1, pad ? pad : 1);
    for (int y = h - 1; y >= 0; y--) {
        fwrite(bgr + (size_t)y * row, 1, row, f);
        if (pad) fwrite(padrow, 1, pad, f);
    }
    free(padrow);
    fclose(f);
    return 1;
}

struct Color { int valid; double b, g, r, spread; int pass; };

static int nv12_to_bgr(const uint8_t *nv12, int w, int h, int ystride, size_t uv_off, uint8_t *bgr) {
    int hstride = ystride > 0 ? (int)(uv_off / (size_t)ystride) : h;
    rga_buffer_t src = wrapbuffer_virtualaddr((void *)nv12, w, h, RK_FORMAT_YCbCr_420_SP, ystride, hstride);
    rga_buffer_t dst = wrapbuffer_virtualaddr(bgr, w, h, RK_FORMAT_BGR_888, w, h);
    IM_STATUS st = imcvtcolor(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_BGR_888, IM_YUV_TO_RGB_BT601_FULL);
    return st == IM_STATUS_NOERROR || st == IM_STATUS_SUCCESS;
}

static struct Color check_color(const uint8_t *bgr, int w, int h) {
    struct Color c; memset(&c, 0, sizeof(c));
    long long sb = 0, sg = 0, sr = 0, n = (long long)w * h;
    if (n <= 0) return c;
    for (long long i = 0; i < n; i++) { sb += bgr[i*3+0]; sg += bgr[i*3+1]; sr += bgr[i*3+2]; }
    c.b = (double)sb / n; c.g = (double)sg / n; c.r = (double)sr / n;
    double mn = c.b < c.g ? c.b : c.g; if (c.r < mn) mn = c.r;
    double mx = c.b > c.g ? c.b : c.g; if (c.r > mx) mx = c.r;
    c.spread = mx - mn; c.valid = 1; c.pass = (c.g > 2.0) && (c.spread < 12.0);
    return c;
}

int main(int argc, char **argv) {
    const char *source = "test", *url = "", *file = "", *out = "", *snapshot = "";
    int duration = 8;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--source") && i+1 < argc) source = argv[++i];
        else if (!strcmp(argv[i], "--url") && i+1 < argc) url = argv[++i];
        else if (!strcmp(argv[i], "--file") && i+1 < argc) file = argv[++i];
        else if (!strcmp(argv[i], "--duration") && i+1 < argc) duration = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i+1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--snapshot") && i+1 < argc) snapshot = argv[++i];
    }
    gst_init(&argc, &argv);

    char head[2048];
    if (!strcmp(source, "test"))
        snprintf(head, sizeof(head), "videotestsrc is-live=true pattern=solid-color foreground-color=0x808080 "
                 "! videoconvert ! video/x-raw,format=NV12,width=1280,height=720,framerate=25/1");
    else if (!strcmp(source, "file"))
        snprintf(head, sizeof(head), "filesrc location=\"%s\" ! decodebin ! video/x-raw,format=NV12", file);
    else
        snprintf(head, sizeof(head), "rtspsrc location=\"%s\" latency=500 timeout=5000000 "
                 "udp-buffer-size=4194304 ! decodebin ! video/x-raw,format=NV12", url);

    char pipe[4096];
    snprintf(pipe, sizeof(pipe), "%s ! tee name=t "
             "t. ! queue leaky=downstream max-size-buffers=1 ! fakesink name=count_sink sync=false "
             "t. ! queue leaky=downstream max-size-buffers=1 ! appsink name=frame_sink drop=1 max-buffers=1 sync=false",
             head);

    GError *err = NULL;
    GstElement *pipeline = gst_parse_launch(pipe, &err);
    char errors[8][256]; int nerr = 0;
    if (err) { snprintf(errors[nerr++], 256, "pipeline parse: %s", err->message); g_error_free(err); }
    if (!pipeline) { snprintf(errors[nerr++], 256, "pipeline build failed"); }

    GstElement *count_sink = pipeline ? gst_bin_get_by_name(GST_BIN(pipeline), "count_sink") : NULL;
    if (count_sink) {
        GstPad *pad = gst_element_get_static_pad(count_sink, "sink");
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, count_probe, NULL, NULL);
        gst_object_unref(pad);
    }
    GstElement *frame_sink = pipeline ? gst_bin_get_by_name(GST_BIN(pipeline), "frame_sink") : NULL;

    int played = 0;
    long long frames_display = 0;
    struct Color color; memset(&color, 0, sizeof(color));
    char snap_saved[512] = "";
    long long ticks0 = read_cpu_ticks();
    int64_t t0 = now_ms();
    double gpu_max = -1, gpu_sum = 0; int gpu_n = 0;
    int64_t last_gpu_ms = 0;
    double seconds = 0;

    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        GstState st = GST_STATE_VOID_PENDING, pend = GST_STATE_VOID_PENDING;
        gst_element_get_state(pipeline, &st, &pend, 8 * GST_SECOND);
        played = (st == GST_STATE_PLAYING);
        unsigned char *bgr = NULL;
        size_t bgr_cap = 0;
        int first = 1;
        while (now_ms() - t0 < (int64_t)duration * 1000) {
            GstSample *s = gst_app_sink_try_pull_sample(GST_APP_SINK(frame_sink), 2 * GST_SECOND);
            if (!s) { if (gst_app_sink_is_eos(GST_APP_SINK(frame_sink))) break; else continue; }
            GstCaps *caps = gst_sample_get_caps(s);
            GstVideoInfo info;
            GstBuffer *buf = gst_sample_get_buffer(s);
            GstVideoFrame vf;
            if (gst_video_info_from_caps(&info, caps) && gst_video_frame_map(&vf, &info, buf, GST_MAP_READ)) {
                int w = info.width, h = info.height;
                size_t need = (size_t)w * h * 3;
                if (need > bgr_cap) { bgr = (unsigned char *)realloc(bgr, need); bgr_cap = need; }
                if (!bgr) { gst_video_frame_unmap(&vf); gst_sample_unref(s); break; }
                memset(bgr, 0, need);
                size_t uv_off = (size_t)vf.info.offset[1];
                if (nv12_to_bgr((const uint8_t *)vf.data[0], w, h, vf.info.stride[0], uv_off, bgr)) {
                    frames_display++;
                    if (first) {
                        first = 0;
                        color = check_color(bgr, w, h);
                        if (snapshot[0] && write_bmp(snapshot, bgr, w, h))
                            snprintf(snap_saved, sizeof(snap_saved), "%s", snapshot);
                    }
                } else if (nerr < 8) {
                    snprintf(errors[nerr++], 256, "rga convert failed");
                }
                gst_video_frame_unmap(&vf);
            }
            gst_sample_unref(s);
            int64_t tnow = now_ms();
            if (tnow - last_gpu_ms >= 500) {
                last_gpu_ms = tnow;
                double l = read_gpu_load_pct();
                if (l >= 0) { gpu_sum += l; gpu_n++; if (l > gpu_max) gpu_max = l; }
            }
        }
        seconds = (now_ms() - t0) / 1000.0;
        gst_element_set_state(pipeline, GST_STATE_NULL);
        free(bgr);
    }

    long long ticks1 = read_cpu_ticks();
    double cpu_pct = -1; long hz = sysconf(_SC_CLK_TCK);
    if (ticks0 >= 0 && ticks1 >= 0 && hz > 0 && seconds > 0)
        cpu_pct = (double)(ticks1 - ticks0) / hz / seconds * 100.0;

    char json[8192];
    int p = snprintf(json, sizeof(json),
        "{\n  \"variant\": \"cpu_headless_noqt\",\n  \"path\": \"cpu\",\n"
        "  \"source\": \"%s\",\n  \"url\": \"%s\",\n  \"file\": \"%s\",\n"
        "  \"duration_s\": %.2f,\n  \"playing\": %s,\n"
        "  \"frames_source\": %llu,\n  \"fps_source\": %.2f,\n"
        "  \"frames_display\": %lld,\n  \"fps_display\": %.2f,\n"
        "  \"cpu_pct_one_core\": %.2f,\n  \"rss_mb\": %.1f,\n"
        "  \"gpu_load_max\": %.2f,\n  \"gpu_load_avg\": %.2f,\n  \"gpu_samples\": %d,\n"
        "  \"color\": {\"valid\": %s, \"b_mean\": %.2f, \"g_mean\": %.2f, \"r_mean\": %.2f, \"spread\": %.2f, \"pass\": %s},\n"
        "  \"snapshot\": \"%s\",\n  \"negotiated\": %s,\n  \"errors\": [",
        source, url, file, seconds, played ? "true" : "false",
        g_frames_source, seconds > 0 ? g_frames_source / seconds : 0.0,
        frames_display, seconds > 0 ? frames_display / seconds : 0.0,
        cpu_pct, read_rss_mb(),
        gpu_max, gpu_n ? gpu_sum / gpu_n : -1.0, gpu_n,
        color.valid ? "true" : "false", color.b, color.g, color.r, color.spread, color.pass ? "true" : "false",
        snap_saved, (g_frames_source > 0) ? "true" : "false");
    for (int i = 0; i < nerr && p < (int)sizeof(json) - 64; i++)
        p += snprintf(json + p, sizeof(json) - p, "%s\"%s\"", i ? ", " : "", errors[i]);
    snprintf(json + p, sizeof(json) - p, "]\n}\n");

    fputs(json, stdout);
    if (out[0]) { FILE *f = fopen(out, "w"); if (f) { fputs(json, f); fclose(f); } }
    if (pipeline) gst_object_unref(pipeline);
    return 0;
}
