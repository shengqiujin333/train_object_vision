// railhmi-frame-lease (视频 ITEM-004)
// 验证不可变帧租约与缓冲区所有权：
//   * 无帧时 latest_frame() 返回 NOT_READY；
//   * 每帧按 caps 生成 FrameDescriptor（planes offset/stride/size + buffer_bytes），
//     非 NV12/非法 stride 显式拒绝（core::validate_layout）；
//   * 多消费者并发读取同一帧（共享租约）不发生 use-after-free；
//   * release 回调在最后一个消费者退出后恰好触发一次。
//
// 用法:
//   railhmi-frame-lease --source test --duration 3 --consumers 3 --out lease.log
//   railhmi-frame-lease --source rtsp --url 'rtsp://<cam>/…' --user admin --password-env VAR
//   railhmi-frame-lease --selftest
//
// 退出码: 0 正常；1 selftest 失败；2 参数错误；3 启动失败。

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "railhmi/core/frame_layout.h"
#include "railhmi/core/timebase.h"
#include "railhmi/media/camera_stream.h"
#include "railhmi/media/frame_lease.h"

namespace {

std::atomic<long long> g_released{0};

int run_selftest() {
    using namespace railhmi::core;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("selftest %s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    FrameLayout l;
    check(make_nv12_layout(1280, 720, 1408, 1408, 1408ull * 720, &l) == LayoutStatus::Ok,
          "nv12_padded_ok");
    check(make_nv12_layout(1281, 720, 1282, 1282, 1282ull * 720, &l) == LayoutStatus::OddDimensions,
          "nv12_odd_rejected");
    check(make_nv12_layout(1280, 720, 1279, 1280, 1280ull * 720, &l) == LayoutStatus::StrideTooSmall,
          "nv12_stride_rejected");
    check(make_nv12_layout(1280, 720, 1280, 1280, 1280ull * 719, &l) ==
              LayoutStatus::OverlappingPlanes,
          "nv12_overlap_rejected");
    check(make_packed_layout(PixelFormat::Bgr888, 640, 480, 640ull * 3, &l) == LayoutStatus::Ok,
          "bgr888_ok");
    check(make_packed_layout(PixelFormat::Nv12, 640, 480, 1920, &l) == LayoutStatus::UnsupportedFormat,
          "unsupported_format_rejected");
    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;
    media::CameraStreamConfig cfg;
    std::string src = "test";
    long long duration_s = 3;
    int consumers = 3;
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
        else if (a == "--consumers") { std::string v; next(&v); consumers = std::atoi(v.c_str()); }
        else if (a == "--out") next(&out);
        else if (a == "--selftest") return run_selftest();
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-frame-lease --source test|file|rtsp [--file f] [--url u] "
                        "[--user u] [--password-env VAR] [--duration s] [--consumers n] [--out log] "
                        "[--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }
    if (src == "file") cfg.source = media::SourceKind::File;
    else if (src == "rtsp") cfg.source = media::SourceKind::Rtsp;
    else cfg.source = media::SourceKind::Test;

    core::TimeBase tb;
    tb.calibrate(core::TimeBase::system_now_ms(), core::TimeBase::steady_now_us());
    cfg.timebase = &tb;
    if (cfg.session_id.empty()) cfg.session_id = core::make_session_id();

    media::CameraStream stream(cfg);
    stream.frame_store().set_release_callback(
        [](const media::FrameDescriptor&) { g_released.fetch_add(1); });

    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) fo << line << "\n";
    };

    // 1) 无帧 -> NOT_READY。
    {
        media::FrameLease l = stream.latest_frame();
        emit(std::string("type=lease_pre not_ready=") + (l.valid() ? "false" : "true") +
             " status=" + l.status() + " plane_count=" + std::to_string(l.plane_count()));
    }

    std::string err;
    if (!stream.start(&err)) {
        std::fprintf(stderr, "stream start failed: %s\n", err.c_str());
        return 3;
    }
    emit("type=injection source=" + src + " camera=" + cfg.camera_id +
         " consumers=" + std::to_string(consumers) + " duration_s=" + std::to_string(duration_s) +
         " session_id=" + cfg.session_id);

    // 2) 等首帧并校验 descriptor。
    media::FrameLease first;
    for (int i = 0; i < 200 && !first.valid(); ++i) {
        stream.pump(50);
        first = stream.latest_frame();
    }
    if (!first.valid()) {
        emit("type=lease_first valid=false");
        stream.stop();
        return 3;
    }
    {
        const media::FrameDescriptor& d = first.descriptor();
        const bool planes_ok = d.layout.planes.size() == 2 &&
            d.layout.planes[0].stride_bytes >= static_cast<std::uint64_t>(d.layout.width) &&
            d.layout.planes[1].offset >= d.layout.planes[0].size_bytes &&
            core::validate_layout(d.layout) == core::LayoutStatus::Ok;
        emit("type=lease_first valid=true camera=" + d.camera_id + " origin=" + d.origin +
             " format=" + core::to_string(d.layout.format) + " size=" + std::to_string(d.layout.width) +
             "x" + std::to_string(d.layout.height) + " colorimetry=" + d.colorimetry +
             " buffer_ref=" + d.buffer_ref + " memory_kind=" + d.memory_kind +
             " planes=" + std::to_string(d.layout.planes.size()) +
             " buffer_bytes=" + std::to_string(d.layout.buffer_bytes) +
             " plane0_stride=" + std::to_string(d.layout.planes[0].stride_bytes) +
             " plane1_offset=" + std::to_string(d.layout.planes[1].offset) +
             " plane_ok=" + (planes_ok ? "true" : "false"));
    }

    // 3) 多消费者并发读取同一帧。
    std::atomic<bool> stop{false};
    std::atomic<long long> reads{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < consumers; ++i) {
        threads.emplace_back([&]() {
            while (!stop.load()) {
                media::FrameLease l = stream.latest_frame();
                if (l.valid()) {
                    const std::uint8_t* p = l.plane_data(0);
                    if (p) {
                        volatile std::uint8_t b = p[0];
                        (void)b;
                        reads.fetch_add(1);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
    }

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    long long max_use = 0;
    while (std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count() <
           duration_s * 1000) {
        stream.pump(50);
        media::FrameLease l = stream.latest_frame();
        if (l.valid()) max_use = std::max<long long>(max_use, l.use_count());
    }
    stop = true;
    for (auto& t : threads) t.join();

    emit("type=lease_concurrent consumers=" + std::to_string(consumers) +
         " reads=" + std::to_string(reads.load()) + " max_use_count=" + std::to_string(max_use) +
         " published=" + std::to_string(stream.frame_store().published()) +
         " rejected=" + std::to_string(stream.frame_store().rejected()));

    // 4) 共享租约 + 最后一个消费者退出后释放：
    //    held 指向帧 A；继续泵到 A 被新帧 B 替换（store 不再持有 A），
    //    此时只有消费者持有 A，释放最后一个消费者才触发一次 release 回调。
    media::FrameLease held = stream.latest_frame();
    if (held.valid()) {
        const std::uint64_t held_seq = held.descriptor().seq;
        const auto t1 = clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t1).count() <
               1500) {
            stream.pump(20);
            media::FrameLease l = stream.latest_frame();
            if (l.valid() && l.descriptor().seq != held_seq) break;  // 已被新帧替换
        }
        const long long r1 = g_released.load();
        media::FrameLease copy = held;
        media::FrameLease copy2 = held;
        const long long r2 = g_released.load();
        emit("type=lease_shared use_count=" + std::to_string(copy.use_count()) +
             " released_while_shared=" + std::to_string(r2 - r1));
        copy = media::FrameLease();
        copy2 = media::FrameLease();  // 释放两个副本，仍有 held
        const long long r3 = g_released.load();
        emit("type=lease_partial release_after_copies=" + std::to_string(r3 - r2));
        held = media::FrameLease();  // 最后一个消费者退出 -> 释放 A 并触发回调一次
        const long long r4 = g_released.load();
        emit("type=lease_last release_after_last=" + std::to_string(r4 - r3));
    } else {
        emit("type=lease_last valid=false");
    }

    stream.stop();
    emit("type=summary released_total=" + std::to_string(g_released.load()) +
         " final_not_ready=" + ((stream.latest_frame().valid()) ? "false" : "true"));
    return 0;
}
