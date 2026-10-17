// railhmi-ai-dispatch (视频 ITEM-010)
// 验证 AI 帧派发边界（媒体侧，不做模型推理）：
//   * 仅 ai_enabled 且 ai_profile != none 的相机进入推理队列；左右两侧(none)永不入队；
//   * 按 app.ai.inference_interval 降频派发；
//   * 队列有界、丢最旧并计数；
//   * 派发记录携带 (session_id,camera_id,generation,seq) 关联键与 capture 时刻，可用于结果回联。
//
// 用法:
//   railhmi-ai-dispatch --source test --interval 5 --depth 2 --duration 6 --consume-hz 2 \
//       --out ai.log --json ai.json
//   railhmi-ai-dispatch --selftest
//
// 退出码: 0 正常（含验收判定行）；1 selftest 失败；2 参数错误；3 启动失败。

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/inference.h"
#include "railhmi/media/ai_dispatcher.h"
#include "railhmi/media/multi_stream.h"

namespace {

int run_selftest() {
    using namespace railhmi;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("selftest %s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    check(core::frame_due(5, 5) && core::frame_due(10, 5) && !core::frame_due(1, 5) &&
              !core::frame_due(4, 5),
          "interval_gating");
    check(core::frame_due(1, 1) && core::frame_due(7, 0), "interval_always_when_le1");

    std::vector<media::AiCameraSpec> specs = {
        {"CAM-01", true, "front"}, {"CAM-02", false, "none"}, {"CAM-04", true, "pantograph"}};
    media::AiFrameRouter router(specs, 5, 2);
    check(router.enabled("CAM-01") && router.enabled("CAM-04") && !router.enabled("CAM-02"),
          "enabled_filtering");
    check(router.enabled("CAM-02") == false, "profile_none_disabled");
    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;
    std::string src = "test";
    std::string config_dir = "config";
    int interval = -1;
    std::size_t depth = 0;
    long long duration_s = 6;
    double consume_hz = 2.0;
    std::string out, json;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--source") next(&src);
        else if (a == "--config") next(&config_dir);
        else if (a == "--interval") { std::string v; next(&v); interval = std::atoi(v.c_str()); }
        else if (a == "--depth") { std::string v; next(&v); depth = static_cast<std::size_t>(std::atoll(v.c_str())); }
        else if (a == "--duration") { std::string v; next(&v); duration_s = std::atoll(v.c_str()); }
        else if (a == "--consume-hz") { std::string v; next(&v); consume_hz = std::atof(v.c_str()); }
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--selftest") return run_selftest();
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-ai-dispatch --source test|file|rtsp [--config dir] [--interval n] "
                        "[--depth n] [--duration s] [--consume-hz f] [--out log] [--json json] [--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }

    // 推理选择与降频来自项目配置：cameras[].ai_enabled/ai_profile、app.ai.inference_interval/queue_depth。
    // 不写死设备；配置缺失时明确回退到文档化默认并告警。
    core::ConfigLoadResult cfg = core::load_config_dir(config_dir);
    const bool have_cfg = cfg.ok && !cfg.settings.cameras.empty();
    std::vector<media::AiCameraSpec> ai_specs;
    if (have_cfg) {
        for (const auto& c : cfg.settings.cameras) {
            media::AiCameraSpec s;
            s.camera_id = c.id;
            s.ai_enabled = c.ai_enabled;
            s.ai_profile = core::to_string(c.ai_profile);
            ai_specs.push_back(std::move(s));
        }
        if (interval < 0) interval = cfg.settings.app.ai.inference_interval;
        if (depth == 0) depth = static_cast<std::size_t>(cfg.settings.app.ai.queue_depth);
    } else {
        std::fprintf(stderr, "warning: config dir '%s' unusable", config_dir.c_str());
        for (const auto& e : cfg.errors) std::fprintf(stderr, " [%s]", e.str().c_str());
        std::fprintf(stderr, "; 回退文档化默认\n");
        ai_specs = {{"CAM-01", true, "front"},   {"CAM-02", false, "none"},
                    {"CAM-03", false, "none"},  {"CAM-04", true, "pantograph"},
                    {"CAM-05", true, "wheel_rail"}, {"CAM-06", true, "cab_door"}};
    }
    if (interval < 1) interval = 5;
    if (depth == 0) depth = 2;
    std::vector<media::ChannelSpec> cs_specs;
    for (const auto& a : ai_specs) {
        media::ChannelSpec sp;
        sp.camera_id = a.camera_id;
        sp.cfg.camera_id = a.camera_id;
        sp.cfg.source = (src == "file") ? media::SourceKind::File
                       : (src == "rtsp") ? media::SourceKind::Rtsp
                                         : media::SourceKind::Test;
        cs_specs.push_back(sp);
    }

    media::AiFrameRouter router(ai_specs, interval, depth);
    media::MultiStreamManager mgr(cs_specs);
    for (std::size_t i = 0; i < mgr.size(); ++i) {
        media::CameraWorker* w = mgr.worker(i);
        if (w) {
            w->set_frame_callback([&router](const media::FrameLease& l, bool) { router.on_frame(l); });
        }
    }

    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) fo << line << "\n";
    };

    std::string err;
    const int started = mgr.start_all(&err);
    emit("type=injection source=" + src + " channels=" + std::to_string(mgr.size()) +
         " started=" + std::to_string(started) +
         " inference_interval=" + std::to_string(interval) +
         " queue_depth=" + std::to_string(depth) +
         " consume_hz=" + std::to_string(consume_hz));
    if (started == 0) {
        std::fprintf(stderr, "all channels failed to start: %s\n", err.c_str());
        return 3;
    }

    std::atomic<bool> stop{false};
    std::atomic<long long> popped{0};
    media::AiFrame last_popped;
    std::mutex last_mtx;

    std::thread consumer([&]() {
        while (!stop.load()) {
            for (const auto& a : ai_specs) {
                media::AiFrame f;
                if (router.pop(a.camera_id, &f)) {
                    popped.fetch_add(1);
                    std::lock_guard<std::mutex> lk(last_mtx);
                    last_popped = f;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(
                static_cast<int>(1000.0 / (consume_hz > 0 ? consume_hz : 1))));
        }
    });

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    long long prev_slot = -1;
    while (true) {
        const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
        if (elapsed >= duration_s * 1000) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const long long slot = elapsed / 500;
        if (slot == prev_slot) continue;
        prev_slot = slot;
        std::string line = "t=" + std::to_string(elapsed) + " type=ai";
        for (const auto& a : ai_specs) {
            line += " [" + a.camera_id + " enq=" + std::to_string(router.enqueued(a.camera_id)) +
                    " drop=" + std::to_string(router.dropped(a.camera_id)) +
                    " skip=" + std::to_string(router.skipped_interval(a.camera_id)) +
                    " rej=" + std::to_string(router.rejected_disabled(a.camera_id)) +
                    " depth=" + std::to_string(router.depth(a.camera_id)) + "]";
        }
        line += " popped=" + std::to_string(popped.load());
        emit(line);
    }
    stop.store(true);
    consumer.join();

    // 逐路汇总与判据。
    const std::vector<media::StreamStats> st = mgr.stats();
    bool enabled_dispatch = true;
    bool disabled_never = true;
    bool interval_applied = true;
    bool bounded = true;
    for (std::size_t i = 0; i < mgr.size(); ++i) {
        const std::string& id = mgr.ids()[i];
        const std::uint64_t published = st[i].last_seq;
        const std::uint64_t enq = router.enqueued(id);
        emit("type=ai_summary camera=" + id +
             " enabled=" + std::string(router.enabled(id) ? "true" : "false") +
             " published=" + std::to_string(published) +
             " enqueued=" + std::to_string(enq) +
             " dropped=" + std::to_string(router.dropped(id)) +
             " skipped=" + std::to_string(router.skipped_interval(id)) +
             " rejected=" + std::to_string(router.rejected_disabled(id)) +
             " depth=" + std::to_string(router.depth(id)));
        if (router.enabled(id)) {
            if (enq == 0) enabled_dispatch = false;
            const std::uint64_t expect = published / static_cast<std::uint64_t>(interval > 0 ? interval : 1);
            // 降频：入队数应约为 published/interval（允许少量边界差）。
            if (enq + 2 < expect || enq > expect + 2) interval_applied = false;
            if (router.depth(id) > depth) bounded = false;
        } else {
            if (enq != 0) disabled_never = false;
            if (router.rejected_disabled(id) == 0) disabled_never = false;
        }
    }
    const bool dropped_counted = router.total_enqueued() > 0;  // 由下列具体判据补充
    bool any_dropped = false;
    for (const auto& a : ai_specs) if (router.enabled(a.camera_id) && router.dropped(a.camera_id) > 0) any_dropped = true;
    const bool overflow_counted = any_dropped;
    bool identity_present = false;
    {
        std::lock_guard<std::mutex> lk(last_mtx);
        identity_present = !last_popped.session_id.empty() && !last_popped.camera_id.empty() &&
                           last_popped.generation >= 1 && last_popped.seq >= 1;
        emit("type=popped camera=" + last_popped.camera_id + " session=" + last_popped.session_id +
             " generation=" + std::to_string(last_popped.generation) +
             " seq=" + std::to_string(last_popped.seq) + " profile=" + last_popped.profile +
             " capture_mono_us=" + std::to_string(last_popped.capture_mono_us));
    }
    (void)dropped_counted;

    const bool ok = enabled_dispatch && disabled_never && interval_applied && bounded && overflow_counted &&
                    identity_present;
    emit("type=acceptance enabled_only_dispatch=" + std::string(enabled_dispatch ? "true" : "false") +
         " disabled_never_enqueued=" + std::string(disabled_never ? "true" : "false") +
         " interval_applied=" + std::string(interval_applied ? "true" : "false") +
         " bounded=" + std::string(bounded ? "true" : "false") +
         " overflow_counted=" + std::string(overflow_counted ? "true" : "false") +
         " identity_present=" + std::string(identity_present ? "true" : "false") +
         " result=" + std::string(ok ? "PASS" : "FAIL"));
    mgr.stop_all();

    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        j << "{\n  \"interval\": " << interval << ", \"queue_depth\": " << depth
          << ", \"total_enqueued\": " << router.total_enqueued()
          << ", \"acceptance\": " << (ok ? "true" : "false") << "\n}\n";
    }
    return 0;
}
