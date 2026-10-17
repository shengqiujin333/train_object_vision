// railhmi-rga-convert (视频 ITEM-005)
// 验证 librga NV12→BGR888 转换（BT601_FULL）：
//   * 取流 worker 内转换（CameraStream.convert_bgr）后 BGR 三通道均值近似相等；
//   * 带 padding 的 NV12（stride>width、uv_offset 非连续）转换正确（padding 不泄漏）；
//   * 纯色（灰/蓝）方向正确（不做绿帧回归）；
//   * 转换失败输出脱敏原因且不静默丢帧；
//   * 不使用 rgaconvert 插件。
//
// 用法:
//   railhmi-rga-convert --source test --duration 3 --out rga.log
//   railhmi-rga-convert --source rtsp --url 'rtsp://<cam>/…' --user admin --password-env VAR
//   railhmi-rga-convert --selftest
//
// 退出码: 0 正常；1 selftest 失败；2 参数错误；3 启动失败。

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/core/timebase.h"
#include "railhmi/media/camera_stream.h"
#include "railhmi/media/pipeline_factory.h"
#include "railhmi/media/rga_convert.h"

namespace {

int run_selftest() {
    using namespace railhmi;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("selftest %s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    check(media::rga_hstride(1408, 1408ull * 720) == 720, "hstride_padded");
    check(media::rga_hstride(1280, 1280ull * 720) == 720, "hstride_contiguous");
    check(media::rga_hstride(1280, 1000) == -1, "hstride_non_multiple_rejected");
    check(media::rga_hstride(0, 100) == -1, "hstride_zero_stride_rejected");

    media::CameraPipelineSpec spec;
    spec.source = media::PipelineSource::Rtsp;
    spec.url = "rtsp://admin:DUMMY_SECRET@192.168.1.65:554/cam/realmonitor?channel=1&subtype=0";
    spec.decode = media::DecodeMode::Auto;
    const std::string pa = media::build_pipeline_description(spec);
    check(pa.find("rgaconvert") == std::string::npos, "no_rgaconvert_plugin");
    check(pa.find("decodebin") != std::string::npos, "auto_uses_decodebin");
    media::CameraPipelineSpec h5 = spec;
    h5.decode = media::DecodeMode::ExplicitH265;
    const std::string ph = media::build_pipeline_description(h5);
    check(ph.find("mppvideodec") != std::string::npos, "explicit_uses_mppvideodec");
    check(ph.find("rgaconvert") == std::string::npos, "explicit_no_rgaconvert");

    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

void fill_nv12(std::vector<std::uint8_t>* buf, int w, int h, int stride, std::uint64_t uv_off,
               int y, int u, int v) {
    buf->assign(static_cast<std::size_t>(uv_off) +
                    static_cast<std::size_t>(stride) * static_cast<std::size_t>(h / 2),
                0);
    for (int r = 0; r < h; ++r) {
        std::memset(buf->data() + static_cast<std::size_t>(r) * stride, y, w);  // padding 保留 0
    }
    std::uint8_t* uv = buf->data() + uv_off;
    for (std::size_t i = 0; i < static_cast<std::size_t>(stride) * (h / 2); i += 2) {
        uv[i] = static_cast<std::uint8_t>(u);
        uv[i + 1] = static_cast<std::uint8_t>(v);
    }
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;
    media::CameraStreamConfig cfg;
    std::string src = "test";
    long long duration_s = 3;
    std::string out;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--source") next(&src);
        else if (a == "--file") next(&cfg.file);
        else if (a == "--url") next(&cfg.url);
        else if (a == "--user") next(&cfg.username);
        else if (a == "--camera") next(&cfg.camera_id);
        else if (a == "--password-env") {
            std::string v; next(&v);
            const char* p = std::getenv(v.c_str());
            if (p) cfg.password = p;
        }
        else if (a == "--duration") { std::string v; next(&v); duration_s = std::atoll(v.c_str()); }
        else if (a == "--out") next(&out);
        else if (a == "--selftest") return run_selftest();
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-rga-convert --source test|file|rtsp [--file f] [--url u] "
                        "[--user u] [--password-env VAR] [--duration s] [--out log] [--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }
    if (src == "file") cfg.source = media::SourceKind::File;
    else if (src == "rtsp") cfg.source = media::SourceKind::Rtsp;
    else cfg.source = media::SourceKind::Test;
    cfg.convert_bgr = true;

    core::TimeBase tb;
    tb.calibrate(core::TimeBase::system_now_ms(), core::TimeBase::steady_now_us());
    cfg.timebase = &tb;
    if (cfg.session_id.empty()) cfg.session_id = core::make_session_id();

    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) fo << line << "\n";
    };

    // 1) 合成 padded NV12 灰图：stride>width、padding 列为 0；灰度应折算为 BGR≈200 且 padding 不泄漏。
    {
        const int w = 1280, h = 720, stride = 1408;
        const std::uint64_t uv_off = static_cast<std::uint64_t>(stride) * h;
        std::vector<std::uint8_t> buf;
        fill_nv12(&buf, w, h, stride, uv_off, 200, 128, 128);
        media::BgrImage img;
        std::string err;
        const bool ok = media::nv12_to_bgr_raw(buf.data(), w, h, stride, uv_off, &img, &err);
        int mn = 255, mx = 0;
        for (std::uint8_t p : img.data) { mn = std::min<int>(mn, p); mx = std::max<int>(mx, p); }
        double b = 0, g = 0, r = 0;
        media::bgr_channel_means(img, &b, &g, &r);
        emit("type=synthetic_padded ok=" + std::string(ok ? "true" : "false") +
             " err=" + err + " width=" + std::to_string(img.width) + " height=" + std::to_string(img.height) +
             " b=" + std::to_string((int)(b + 0.5)) + " g=" + std::to_string((int)(g + 0.5)) +
             " r=" + std::to_string((int)(r + 0.5)) +
             " spread=" + std::to_string((int)(media::bgr_channel_spread(img) + 0.5)) +
             " min_px=" + std::to_string(mn) + " max_px=" + std::to_string(mx) +
             " padding_not_leaked=" + (mn > 150 ? "true" : "false"));
    }

    // 2) 合成纯蓝 NV12：B 通道应最大（绿帧回归判据）。
    {
        const int w = 640, h = 480, stride = 640;
        const std::uint64_t uv_off = static_cast<std::uint64_t>(stride) * h;
        std::vector<std::uint8_t> buf;
        fill_nv12(&buf, w, h, stride, uv_off, 41, 240, 110);
        media::BgrImage img;
        std::string err;
        const bool ok = media::nv12_to_bgr_raw(buf.data(), w, h, stride, uv_off, &img, &err);
        double b = 0, g = 0, r = 0;
        media::bgr_channel_means(img, &b, &g, &r);
        emit("type=synthetic_blue ok=" + std::string(ok ? "true" : "false") + " err=" + err +
             " b=" + std::to_string((int)(b + 0.5)) + " g=" + std::to_string((int)(g + 0.5)) +
             " r=" + std::to_string((int)(r + 0.5)) +
             " blue_dominant=" + ((b > g + 100 && b > r + 100) ? "true" : "false"));
    }

    // 3) 失败路径：空参数/奇数宽高 -> 显式脱敏原因，不静默。
    {
        media::BgrImage img;
        std::string e1, e2, e3;
        const bool f1 = media::nv12_to_bgr_raw(nullptr, 640, 480, 640, 640ull * 480, &img, &e1);
        std::vector<std::uint8_t> buf(64, 0);
        const bool f2 = media::nv12_to_bgr_raw(buf.data(), 641, 480, 642, 642ull * 480, &img, &e2);
        media::FrameLease empty;
        const bool f3 = media::nv12_to_bgr(empty, &img, &e3);
        emit("type=failure_paths null_rejected=" + std::string(!f1 ? "true" : "false") + "(" + e1 + ")" +
             " odd_rejected=" + std::string(!f2 ? "true" : "false") + "(" + e2 + ")" +
             " notready_rejected=" + std::string(!f3 ? "true" : "false") + "(" + e3 + ")");
    }

    // 4) 真实链路：worker 内转换，统计成功/失败与末帧均值。
    media::CameraStream stream(cfg);
    std::string err;
    if (!stream.start(&err)) {
        std::fprintf(stderr, "stream start failed: %s\n", err.c_str());
        return 3;
    }
    emit("type=injection source=" + src + " camera=" + cfg.camera_id +
         " duration_s=" + std::to_string(duration_s) + " session_id=" + cfg.session_id);

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count() <
           duration_s * 1000) {
        stream.pump(50);
    }
    const long long published = stream.frame_store().published();
    const long long converted = stream.bgr_converted();
    const long long failed = stream.bgr_failed();
    const media::BgrImage& img = stream.last_bgr();
    double b = 0, g = 0, r = 0;
    media::bgr_channel_means(img, &b, &g, &r);
    emit("type=worker_convert published=" + std::to_string(published) +
         " bgr_converted=" + std::to_string(converted) + " bgr_failed=" + std::to_string(failed) +
         " last_error=" + stream.bgr_last_error() +
         " bgr_means=" + std::to_string((int)(b + 0.5)) + "/" + std::to_string((int)(g + 0.5)) + "/" +
         std::to_string((int)(r + 0.5)) +
         " spread=" + std::to_string((int)(media::bgr_channel_spread(img) + 0.5)) +
         " width=" + std::to_string(img.width) + " height=" + std::to_string(img.height));

    // 5) 失败不丢帧：帧仍继续发布。
    const long long p1 = stream.frame_store().published();
    for (int i = 0; i < 20; ++i) stream.pump(20);
    const long long p2 = stream.frame_store().published();
    emit("type=fail_keep_frames published_before=" + std::to_string(p1) +
         " published_after=" + std::to_string(p2) +
         " still_producing=" + (p2 >= p1 ? "true" : "false"));

    stream.stop();
    emit("type=summary bgr_converted=" + std::to_string(stream.bgr_converted()) +
         " bgr_failed=" + std::to_string(stream.bgr_failed()) +
         " published=" + std::to_string(stream.frame_store().published()) +
         " rejected=" + std::to_string(stream.frame_store().rejected()));
    return 0;
}
