// railhmi-camera-worker (视频 ITEM-006)
// 验证每路取流 worker 与六态机接入：
//   * 专用 worker 线程有限等待拉帧（单次 pull ≤200ms）、drain bus/事件、驱动六态机；
//   * 断流(FI-01)/冻结(FI-02) 后进入 FROZEN/RECONNECTING 并按阈值升级 OFFLINE；
//   * 真正重建后 generation+1、计数清零、恢复出帧；
//   * 渲染前判定 presentable = may_present_as_live(now) 且租约属于当前 generation：
//     非 LIVE 时 presentable 必为 false（旧帧不当实时）。
//
// 用法:
//   railhmi-camera-worker --source test --duration 9 --freeze-after 1 --restart-after 6 \
//       --freeze-timeout-ms 500 --first-frame-timeout-ms 500 --backoff-ms 200 \
//       --reconnect-threshold 3 --out worker.log
//   railhmi-camera-worker --source rtsp --url 'rtsp://<cam>/…' --user admin --password-env VAR
//   railhmi-camera-worker --selftest
//
// 退出码: 0 正常；1 selftest 失败；2 参数错误；3 启动失败。

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

#include "railhmi/core/stream_state.h"
#include "railhmi/core/timebase.h"
#include "railhmi/media/camera_worker.h"

namespace {

int run_selftest() {
    using namespace railhmi;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("selftest %s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };

    // 六态机：live→frozen→reconnecting→offline，恢复后计数清零。
    core::StreamThresholds th;
    th.freeze_timeout_ms = 100;
    th.first_frame_timeout_ms = 100;
    th.reconnect_threshold = 3;
    th.reconnect_backoff_ms = 50;
    core::StreamStateMachine m("CAM-99", th);
    m.start(0);
    core::QualityFlags ok;
    m.on_frame(10, ok);
    check(m.state() == core::StreamState::Live, "sm_live");
    m.tick(50);
    check(m.state() == core::StreamState::Live, "sm_live_no_freeze_before_timeout");
    m.tick(120);
    check(m.state() == core::StreamState::Frozen, "sm_freeze_after_timeout");
    m.tick(230);
    check(m.state() == core::StreamState::Reconnecting && m.reconnect_count() == 1,
          "sm_frozen_to_reconnecting");
    m.tick(290);  // backoff(50) -> connecting
    m.tick(400);  // first frame timeout -> reconnect #2
    m.tick(460);  // backoff -> connecting
    m.tick(570);  // timeout -> reconnect #3 -> offline + escalated
    check(m.state() == core::StreamState::Offline && m.reconnect_count() == 3, "sm_escalated_offline");
    m.on_frame(600, ok);  // 恢复
    check(m.state() == core::StreamState::Live && m.reconnect_count() == 0, "sm_recovery_resets_count");

    // pull 上限钳制（≤200ms）。
    media::CameraStreamConfig cfg;
    media::CameraWorker w(cfg, 9999);
    check(w.pull_timeout_ms() == 200, "pull_timeout_clamped_to_200");
    media::CameraWorker w2(cfg, 0);
    check(w2.pull_timeout_ms() == 1, "pull_timeout_floor_to_1");

    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;
    media::CameraStreamConfig cfg;
    std::string src = "test";
    long long duration_s = 6;
    long long freeze_after_s = -1;
    long long cut_after_s = -1;
    long long restart_after_s = -1;
    long long resume_after_s = -1;
    int pull_ms = 200;
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
        else if (a == "--freeze-after") { std::string v; next(&v); freeze_after_s = std::atoll(v.c_str()); }
        else if (a == "--cut-after") { std::string v; next(&v); cut_after_s = std::atoll(v.c_str()); }
        else if (a == "--restart-after") { std::string v; next(&v); restart_after_s = std::atoll(v.c_str()); }
        else if (a == "--resume-after") { std::string v; next(&v); resume_after_s = std::atoll(v.c_str()); }
        else if (a == "--pull-ms") { std::string v; next(&v); pull_ms = std::atoi(v.c_str()); }
        else if (a == "--freeze-timeout-ms") { std::string v; next(&v); cfg.thresholds.freeze_timeout_ms = std::atoi(v.c_str()); }
        else if (a == "--first-frame-timeout-ms") { std::string v; next(&v); cfg.thresholds.first_frame_timeout_ms = std::atoi(v.c_str()); }
        else if (a == "--reconnect-threshold") { std::string v; next(&v); cfg.thresholds.reconnect_threshold = std::atoi(v.c_str()); }
        else if (a == "--backoff-ms") { std::string v; next(&v); cfg.thresholds.reconnect_backoff_ms = std::atoi(v.c_str()); }
        else if (a == "--out") next(&out);
        else if (a == "--selftest") return run_selftest();
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-camera-worker --source test|file|rtsp [--file f] [--url u] "
                        "[--user u] [--password-env VAR] [--duration s] [--freeze-after s] "
                        "[--cut-after s] [--restart-after s] [--resume-after s] [--pull-ms n] [--freeze-timeout-ms n] "
                        "[--first-frame-timeout-ms n] [--backoff-ms n] [--reconnect-threshold n] "
                        "[--out log] [--selftest]\n");
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

    media::CameraWorker worker(cfg, pull_ms);
    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) fo << line << "\n";
    };

    std::string err;
    if (!worker.start(&err)) {
        std::fprintf(stderr, "worker start failed: %s\n", err.c_str());
        return 3;
    }
    emit("type=injection source=" + src + " camera=" + cfg.camera_id +
         " duration_s=" + std::to_string(duration_s) +
         " freeze_after_s=" + std::to_string(freeze_after_s) +
         " cut_after_s=" + std::to_string(cut_after_s) +
         " restart_after_s=" + std::to_string(restart_after_s) +
         " resume_after_s=" + std::to_string(resume_after_s) +
         " pull_ms=" + std::to_string(worker.pull_timeout_ms()) +
         " session_id=" + cfg.session_id);

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    long long prev_slot = -1;
    bool freeze_done = false, cut_done = false, restart_done = false, resume_done = false;
    std::string prev_state = "?";
    long long nonlive_presentable = 0;
    long long state_transitions = 0;
    unsigned long long seen_gen = 0;

    while (true) {
        const long long elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
        if (elapsed >= duration_s * 1000) break;

        if (freeze_after_s >= 0 && !freeze_done && elapsed >= freeze_after_s * 1000) {
            worker.request_freeze();
            freeze_done = true;
            emit("t=" + std::to_string(elapsed) + " type=injection freeze=FI-02");
        }
        if (cut_after_s >= 0 && !cut_done && elapsed >= cut_after_s * 1000) {
            worker.request_cut();
            cut_done = true;
            emit("t=" + std::to_string(elapsed) + " type=injection cut=FI-01");
        }
        if (restart_after_s >= 0 && !restart_done && elapsed >= restart_after_s * 1000) {
            worker.request_restart();
            restart_done = true;
            emit("t=" + std::to_string(elapsed) + " type=injection restart=rebuild");
        }
        if (resume_after_s >= 0 && !resume_done && elapsed >= resume_after_s * 1000) {
            worker.request_resume();
            resume_done = true;
            emit("t=" + std::to_string(elapsed) + " type=injection resume=thaw");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const long long slot = elapsed / 250;
        if (slot == prev_slot) continue;
        prev_slot = slot;

        media::StreamStats s = worker.stats();
        const bool pres = worker.is_presentable();
        const std::string st = core::to_string(s.state);
        if (st != prev_state) {
            ++state_transitions;
            prev_state = st;
        }
        if (s.state != core::StreamState::Live && pres) ++nonlive_presentable;
        if (s.generation != seen_gen && s.generation != 0) seen_gen = s.generation;

        emit("t=" + std::to_string(elapsed) + " type=worker_state state=" + st +
             " presentable=" + std::string(pres ? "true" : "false") +
             " fps=" + std::to_string((int)(s.fps + 0.5)) +
             " reconnect_count=" + std::to_string(s.reconnect_count) +
             " generation=" + std::to_string(s.generation) +
             " seq=" + std::to_string(s.last_seq) +
             " quality=" + (s.quality_flags.empty() ? "-" : s.quality_flags) +
             " last_frame_ms=" + std::to_string(s.last_frame_ms) +
             " wall_ms=" + std::to_string(s.capture_wall_ms) +
             " bus_state_changes=" + std::to_string(s.bus_state_changes) +
             (s.last_error.empty() ? "" : " error=" + s.last_error));
    }

    emit("type=summary final_state=" + std::string(core::to_string(worker.stats().state)) +
         " iterations=" + std::to_string(worker.iterations()) +
         " events_total=" + std::to_string(worker.events_total()) +
         " freezes=" + std::to_string(worker.freezes()) +
         " reconnect_attempts=" + std::to_string(worker.reconnect_attempts()) +
         " escalations=" + std::to_string(worker.escalations()) +
         " recoveries=" + std::to_string(worker.recoveries()) +
         " bus_state_changes=" + std::to_string(worker.bus_state_changes()) +
         " state_transitions=" + std::to_string(state_transitions) +
         " nonlive_presentable=" + std::to_string(nonlive_presentable) +
         " final_generation=" + std::to_string(worker.stats().generation));
    worker.stop();
    return 0;
}
