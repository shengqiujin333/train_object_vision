// railhmi-backpressure (视频 ITEM-008)
// 验证三类消费者有界队列与背压：
//   * 显示 latest-only（容量 1）→ display_dropped；
//   * 推理有界丢最旧（容量=ai_queue_depth）→ ai_dropped；
//   * 录像独立有界分支 → recording_overflow（缺口）。
// 人为放慢显示/AI 消费者时取流帧率不塌陷；录像分支不受显示/推理丢帧影响，
// 仅当其自身消费者停止时溢出。
//
// 用法:
//   railhmi-backpressure --source test --duration 8 --display-hz 1 --ai-hz 1 \
//       --stop-recording-after 4 --ai-depth 2 --recording-frames 8 --out bp.log --json bp.json
//   railhmi-backpressure --selftest
//
// 退出码: 0 正常（含验收判定行）；1 selftest 失败；2 参数错误；3 启动失败。

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

#include "railhmi/core/bounded_queue.h"
#include "railhmi/core/timebase.h"
#include "railhmi/media/camera_worker.h"
#include "railhmi/media/frame_dispatcher.h"

namespace {

int run_selftest() {
    using namespace railhmi::core;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("selftest %s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    BoundedQueue<int> display(1);
    display.push(1);
    display.push(2);
    check(display.size() == 1 && display.dropped() == 1, "display_latest_only_drops_one");
    BoundedQueue<int> ai(3);
    for (int i = 0; i < 5; ++i) ai.push(i);
    check(ai.size() == 3 && ai.dropped() == 2, "ai_drop_oldest");
    int v = 0;
    check(ai.pop(&v) && v == 2, "ai_drop_oldest_keeps_newest_window");
    BoundedQueue<int> rec(100, 100);
    rec.push(1, 40);
    rec.push(2, 40);
    rec.push(3, 40);
    check(rec.size() == 2 && rec.bytes() == 80 && rec.dropped() == 1, "recording_byte_budget");
    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;
    std::string src = "test";
    long long duration_s = 8;
    double display_hz = 1.0;
    double ai_hz = 1.0;
    long long stop_recording_after_s = 4;
    int ai_depth = 2;
    std::size_t recording_frames = 8;
    std::string out, json;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--source") next(&src);
        else if (a == "--duration") { std::string v; next(&v); duration_s = std::atoll(v.c_str()); }
        else if (a == "--display-hz") { std::string v; next(&v); display_hz = std::atof(v.c_str()); }
        else if (a == "--ai-hz") { std::string v; next(&v); ai_hz = std::atof(v.c_str()); }
        else if (a == "--stop-recording-after") { std::string v; next(&v); stop_recording_after_s = std::atoll(v.c_str()); }
        else if (a == "--ai-depth") { std::string v; next(&v); ai_depth = std::atoi(v.c_str()); }
        else if (a == "--recording-frames") { std::string v; next(&v); recording_frames = static_cast<std::size_t>(std::atoll(v.c_str())); }
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--selftest") return run_selftest();
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-backpressure --source test|file|rtsp [--duration s] [--display-hz f] "
                        "[--ai-hz f] [--stop-recording-after s] [--ai-depth n] [--recording-frames n] "
                        "[--out log] [--json json] [--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }

    media::CameraStreamConfig cfg;
    cfg.camera_id = "CAM-01";
    cfg.source = (src == "file") ? media::SourceKind::File
                 : (src == "rtsp") ? media::SourceKind::Rtsp
                                   : media::SourceKind::Test;
    cfg.ai_queue_depth = ai_depth;
    cfg.recording_queue_frames = recording_frames;
    core::TimeBase tb;
    tb.calibrate(core::TimeBase::system_now_ms(), core::TimeBase::steady_now_us());
    cfg.timebase = &tb;
    cfg.session_id = core::make_session_id();

    media::CameraWorker worker(cfg, 200);
    std::string err;
    if (!worker.start(&err)) {
        std::fprintf(stderr, "worker start failed: %s\n", err.c_str());
        return 3;
    }
    media::FrameDispatcher& disp = worker.dispatcher();

    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) fo << line << "\n";
    };
    emit("type=injection source=" + src + " duration_s=" + std::to_string(duration_s) +
         " display_hz=" + std::to_string(display_hz) + " ai_hz=" + std::to_string(ai_hz) +
         " stop_recording_after_s=" + std::to_string(stop_recording_after_s) +
         " ai_depth=" + std::to_string(ai_depth) +
         " recording_frames=" + std::to_string(recording_frames) +
         " session_id=" + cfg.session_id);

    std::atomic<bool> stop{false};
    std::atomic<bool> recording_active{true};
    std::atomic<long long> display_reads{0}, ai_reads{0}, recording_reads{0};

    std::thread display_th([&]() {
        while (!stop.load()) {
            media::FrameLease l;
            if (disp.display_latest(&l) && l.valid()) display_reads.fetch_add(1);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<int>(1000.0 / (display_hz > 0 ? display_hz : 1))));
        }
    });
    std::thread ai_th([&]() {
        while (!stop.load()) {
            media::FrameLease l;
            if (disp.ai_pop(&l) && l.valid()) ai_reads.fetch_add(1);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<int>(1000.0 / (ai_hz > 0 ? ai_hz : 1))));
        }
    });
    std::thread rec_th([&]() {
        while (!stop.load()) {
            if (recording_active.load()) {
                media::FrameLease l;
                if (disp.recording_pop(&l) && l.valid()) recording_reads.fetch_add(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    long long prev_slot = -1;
    bool recording_stopped = false;
    std::uint64_t overflow_at_stop = 0;
    std::uint64_t dispatched_begin = disp.dispatched();
    long long dispatched_begin_ms = 0;

    while (true) {
        const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
        if (elapsed >= duration_s * 1000) break;
        if (!recording_stopped && elapsed >= stop_recording_after_s * 1000) {
            overflow_at_stop = disp.recording_overflow();
            recording_active.store(false);
            recording_stopped = true;
            emit("t=" + std::to_string(elapsed) + " type=injection stop_recording overflow_at_stop=" +
                 std::to_string(overflow_at_stop));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        const long long slot = elapsed / 500;
        if (slot == prev_slot) continue;
        prev_slot = slot;
        media::StreamStats s = worker.stats();
        emit("t=" + std::to_string(elapsed) +
             " type=bp dispatched=" + std::to_string(disp.dispatched()) +
             " fps=" + std::to_string((int)(s.fps + 0.5)) +
             " display_dropped=" + std::to_string(disp.display_dropped()) +
             " ai_dropped=" + std::to_string(disp.ai_dropped()) +
             " ai_depth=" + std::to_string(disp.ai_depth()) +
             " recording_overflow=" + std::to_string(disp.recording_overflow()) +
             " recording_depth=" + std::to_string(disp.recording_depth()) +
             " display_reads=" + std::to_string(display_reads.load()) +
             " ai_reads=" + std::to_string(ai_reads.load()) +
             " recording_reads=" + std::to_string(recording_reads.load()));
        if (dispatched_begin_ms == 0) dispatched_begin_ms = elapsed;
    }

    const long long elapsed_end = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
    stop.store(true);
    display_th.join();
    ai_th.join();
    rec_th.join();

    const std::uint64_t dispatched_end = disp.dispatched();
    const double window_s = (elapsed_end - dispatched_begin_ms) / 1000.0;
    const double capture_rate = window_s > 0 ? (dispatched_end - dispatched_begin) / window_s : 0.0;
    const std::uint64_t final_overflow = disp.recording_overflow();
    const std::uint64_t overflow_after_stop = final_overflow - overflow_at_stop;
    worker.stop();

    const bool capture_ok = capture_rate >= 20.0;
    const bool display_dropped_ok = disp.display_dropped() > 0;
    const bool ai_dropped_ok = disp.ai_dropped() > 0;
    const bool recording_independent = (overflow_at_stop == 0);
    const bool recording_overflows_when_stopped = overflow_after_stop > 0;
    const bool ok = capture_ok && display_dropped_ok && ai_dropped_ok && recording_independent &&
                    recording_overflows_when_stopped;

    emit("type=summary capture_rate=" + std::to_string((int)(capture_rate + 0.5)) +
         " dispatched=" + std::to_string(dispatched_end) +
         " display_dropped=" + std::to_string(disp.display_dropped()) +
         " ai_dropped=" + std::to_string(disp.ai_dropped()) +
         " recording_overflow_phase1=" + std::to_string(overflow_at_stop) +
         " recording_overflow_phase2=" + std::to_string(overflow_after_stop) +
         " display_reads=" + std::to_string(display_reads.load()) +
         " ai_reads=" + std::to_string(ai_reads.load()) +
         " recording_reads=" + std::to_string(recording_reads.load()));
    emit("type=acceptance capture_rate_ok=" + std::string(capture_ok ? "true" : "false") +
         " display_dropped_ok=" + std::string(display_dropped_ok ? "true" : "false") +
         " ai_dropped_ok=" + std::string(ai_dropped_ok ? "true" : "false") +
         " recording_independent_of_display_ai=" + std::string(recording_independent ? "true" : "false") +
         " recording_overflows_when_consumer_stopped=" +
         std::string(recording_overflows_when_stopped ? "true" : "false") +
         " result=" + std::string(ok ? "PASS" : "FAIL"));

    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        j << "{\n  \"capture_rate\": " << capture_rate
          << ", \"display_dropped\": " << disp.display_dropped()
          << ", \"ai_dropped\": " << disp.ai_dropped()
          << ", \"recording_overflow_phase1\": " << overflow_at_stop
          << ", \"recording_overflow_phase2\": " << overflow_after_stop
          << ", \"acceptance\": " << (ok ? "true" : "false") << "\n}\n";
    }
    return 0;
}
