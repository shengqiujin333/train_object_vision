// railhmi-vision-pipeline（视觉推理 ITEM-009）
//
// 两类可执行验证：
//   * `--selftest`：确定性链路/线程/调度自检（**替代检测器** stub，不加载 RKNN，不声称精度）：
//       - 生产模式无自造帧（未 submit 时 consumed 恒 0）；
//       - 慢 infer 下 submit/snapshot 不被阻塞、request_stop 立即生效（AI 不可用）；
//       - 有界丢旧队列（depth=2 注入 5 帧 -> 丢 3，队长 <= 2）；
//       - 4 路预算 1 时的无饥饿轮转；温度注入触发降载（标记 injected，不冒充实测）；
//       - track_id 永不为 0；不可用路不产生事件。
//   * `--mode chain`：真实链路（file/test 源 -> FrameLease -> ai_dispatcher 队列 ->
//     RknnVisionDetector -> rknn_run -> envelope -> 语义/规则），报告实测吞吐/延迟/温度。
//     模型/阈值/调度参数全部来自 `--config`（app/config）与 `--models-dir`。
//
// 用法:
//   railhmi-vision-pipeline --selftest [--out log] [--json out.json]
//   railhmi-vision-pipeline --mode chain --config ./config --models-dir ./models \
//       --source test --duration 10 [--lanes CAM-01] [--budget 4] [--inject-temp 90]
//
// 退出码: 0 通过；1 失败；2 参数错误；3 启动失败。

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/inference.h"
#include "railhmi/core/rules.h"
#include "railhmi/core/timebase.h"
#include "railhmi/core/vision_inference_envelope.h"
#include "railhmi/core/vision_worker_schedule.h"
#include "railhmi/media/ai_dispatcher.h"
#include "railhmi/media/camera_stream.h"
#include "railhmi/media/thermal_sensor.h"
#include "railhmi/media/vision_ai_worker.h"
#include "railhmi/media/vision_rknn_detector.h"

#ifndef RAILHMI_CONFIG_DIR
#define RAILHMI_CONFIG_DIR "config"
#endif

using namespace railhmi;

namespace {

int g_fail = 0;
std::vector<std::string> g_json_lines;
std::ofstream g_fo;

void emit(const std::string& line) {
    std::printf("%s\n", line.c_str());
    if (g_fo) g_fo << line << "\n";
}

void check(bool ok, const std::string& name, const std::string& detail = "") {
    emit(std::string("[") + (ok ? "PASS" : "FAIL") + "] " + name +
         (detail.empty() ? "" : (" :: " + detail)));
    if (!ok) ++g_fail;
}

long long mono_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string env_bool(bool v) { return v ? "true" : "false"; }

// 计时统计（用于“不被阻塞”判据）。
struct Latency {
    std::vector<long long> us;
    void add(long long v) { us.push_back(v); }
    long long max_us() const { return us.empty() ? 0 : *std::max_element(us.begin(), us.end()); }
    long long p95_us() const {
        if (us.empty()) return 0;
        std::vector<long long> c = us;
        std::sort(c.begin(), c.end());
        const std::size_t idx = static_cast<std::size_t>(c.size() * 95 / 100);
        return c[std::min(idx, c.size() - 1)];
    }
};

struct LaneSource {
    std::string camera_id;
    std::unique_ptr<media::CameraStream> stream;
    long long submitted = 0;
    std::uint64_t last_seq = 0;
};

media::AiCameraSpec spec_of(const core::CameraSlot& s) {
    media::AiCameraSpec a;
    a.camera_id = s.id;
    a.ai_enabled = s.ai_enabled;
    a.ai_profile = core::to_string(s.ai_profile);
    return a;
}

bool start_stream(media::CameraStreamConfig cfg, LaneSource* out, std::string* err) {
    out->camera_id = cfg.camera_id;
    out->stream.reset(new media::CameraStream(cfg));
    return out->stream->start(err);
}

// 取一帧（有限等待）。
media::FrameLease pump_until_frame(media::CameraStream& s, int timeout_ms = 3000) {
    const long long t0 = mono_ms();
    while (mono_ms() - t0 < timeout_ms) {
        s.pump(20);
        media::FrameLease l = s.latest_frame();
        if (l.valid()) return l;
    }
    return media::FrameLease();
}

// 语义/规则配置（阈值来自 thresholds.yaml，不写死）。
core::SemanticConfig semantic_cfg(const core::ThresholdsConfig& th) {
    core::SemanticConfig sc;
    sc.allowed_labels = {"person", "obstacle"};
    sc.known_labels = {"person", "obstacle"};
    sc.confidence_min = th.confidence_min;
    sc.require_track = true;
    sc.use_envelope_in_roi = true;
    sc.track_policy = core::TrackPolicy::EnvelopeOnly;  // envelope 已带 track_id（ITEM-008）
    sc.threshold_source = "app/config/thresholds.yaml:confidence_min";
    sc.allowed_labels_source = "vision design whitelist person/obstacle";
    return sc;
}

core::RuleConfig rule_cfg(const core::ThresholdsConfig& th) {
    core::RuleConfig rc;
    rc.confirm_frames = th.confirm_frames;
    rc.confirm_window_ms = th.confirm_window_ms;
    rc.confidence_min = th.confidence_min;
    rc.suppress_window_ms = th.suppress_window_ms;
    return rc;
}

// ---------------------------------------------------------------- selftest

int run_selftest(const std::string& config_dir, const std::string& out_json) {
    core::ConfigLoadResult loaded = core::load_config_dir(config_dir);
    if (!loaded.ok) {
        emit("type=error config invalid: " + config_dir);
        for (const auto& e : loaded.errors) emit("  " + e.str());
        return 1;
    }
    const core::AppConfig& app = loaded.settings.app;
    const core::ThresholdsConfig& th = loaded.settings.thresholds;

    emit("type=selftest config=" + config_dir + " engine=stub(substitute,not-rknn)");
    emit("type=selftest note=selftest 不加载 RKNN、不声称精度；链路真实性由 --mode chain 呈证");

    // 0) 复用的真实帧租约（GStreamer test 源；来自取流层，不是合成元数据）。
    media::CameraStreamConfig scfg;
    scfg.camera_id = "CAM-01";
    scfg.source = media::SourceKind::Test;
    scfg.test_width = 320;
    scfg.test_height = 180;
    scfg.test_fps = 10;
    scfg.session_id = core::make_session_id();
    core::TimeBase tb;
    tb.calibrate(core::TimeBase::system_now_ms(), core::TimeBase::steady_now_us());
    scfg.timebase = &tb;
    LaneSource src;
    std::string serr;
    if (!start_stream(scfg, &src, &serr)) {
        emit("type=error stream start failed: " + serr);
        emit("RESULT: FAIL failures=0 reason=ENV_UNAVAILABLE");
        return 3;
    }
    media::FrameLease lease = pump_until_frame(*src.stream);
    check(lease.valid(), "selftest_frame_lease_from_stream",
          lease.valid() ? ("seq=" + std::to_string(lease.descriptor().seq)) : "no frame");
    if (!lease.valid()) {
        src.stream->stop();
        emit("RESULT: FAIL failures=1");
        return 1;
    }
    emit("type=selftest lease format=" + std::string(core::to_string(lease.descriptor().layout.format)) +
         " size=" + std::to_string(lease.descriptor().layout.width) + "x" +
         std::to_string(lease.descriptor().layout.height) + " session=" +
         lease.descriptor().session_id);

    // 1) 生产模式无自造帧：起线程但从不 submit -> consumed 恒 0，steps 增长。
    {
        std::vector<media::AiCameraSpec> cams = {{"CAM-01", true, "front"}};
        media::VisionWorkerConfig wc;
        wc.sched = app.ai.inference_interval > 0
                       ? core::AiSchedulerConfig{app.ai.inference_interval, app.ai.queue_depth,
                                                 app.ai.npu_budget, app.ai.temp_limit_c}
                       : core::AiSchedulerConfig{};
        wc.cameras = cams;
        wc.semantic = semantic_cfg(th);
        wc.rules = rule_cfg(th);
        media::StubVisionDetector::Config stubc;
        stubc.sleep_us = 0;
        auto det = std::make_shared<media::StubVisionDetector>(stubc);
        media::VisionAiWorker worker(wc, det, nullptr, nullptr);
        worker.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        media::VisionWorkerSnapshot s = worker.snapshot();
        worker.stop();
        check(s.steps > 0 && s.infer_calls == 0 && !s.lanes.empty() && s.lanes[0].consumed == 0,
              "prod_no_synthetic_frames", "steps=" + std::to_string(s.steps) + " infer_calls=" +
                                              std::to_string(s.infer_calls));
    }

    // 2) 慢 infer（200ms）不阻塞 submit/snapshot/request_stop。
    {
        std::vector<media::AiCameraSpec> cams = {{"CAM-01", true, "front"}};
        media::VisionWorkerConfig wc;
        wc.sched = {1, 2, 4, 85.0};   // 每帧都推理（interval=1），队列深 2
        wc.cameras = cams;
        wc.semantic = semantic_cfg(th);
        wc.rules = rule_cfg(th);
        media::StubVisionDetector::Config stubc;
        stubc.sleep_us = 200000;      // 模拟单次推理 200ms
        auto det = std::make_shared<media::StubVisionDetector>(stubc);
        media::VisionAiWorker worker(wc, det, nullptr, nullptr);
        worker.start();
        Latency submit_lat;
        Latency snap_lat;
        long long stop_lat = 0;
        const long long t0 = mono_ms();
        while (mono_ms() - t0 < 1200) {
            const long long a = mono_ms();
            worker.submit(lease);
            submit_lat.add((mono_ms() - a) * 1000);
            const long long b = mono_ms();
            media::VisionWorkerSnapshot s = worker.snapshot();
            (void)s;
            snap_lat.add((mono_ms() - b) * 1000);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const media::VisionWorkerSnapshot running = worker.snapshot();
        const long long c = mono_ms();
        worker.request_stop();
        stop_lat = (mono_ms() - c) * 1000;
        const media::VisionWorkerSnapshot stopping = worker.snapshot();
        const long long d = mono_ms();
        worker.stop();
        const long long join_lat = (mono_ms() - d) * 1000;
        check(submit_lat.max_us() < 50000, "submit_not_blocked_by_infer",
              "max=" + std::to_string(submit_lat.max_us()) + "us n=" +
                  std::to_string(submit_lat.us.size()));
        check(snap_lat.p95_us() < 20000, "snapshot_not_blocked_by_infer",
              "p95=" + std::to_string(snap_lat.p95_us()) + "us max=" +
                  std::to_string(snap_lat.max_us()) + "us");
        check(stop_lat < 20000, "request_stop_immediate", std::to_string(stop_lat) + "us");
        check(!stopping.worker_alive &&
                  core::ai_status_text(stopping.ai, mono_ms()) == "AI 不可用",
              "stop_ai_unavailable", core::ai_status_text(stopping.ai, mono_ms()));
        check(join_lat < 400000, "stop_join_bounded_by_one_infer",
              std::to_string(join_lat) + "us (infer=200000us, 只等当前一次)");
        check(running.infer_calls > 0, "slow_infer_did_run",
              "infer_calls=" + std::to_string(running.infer_calls));
        emit("type=selftest blocking submit_p95_us=" + std::to_string(submit_lat.p95_us()) +
             " snapshot_p95_us=" + std::to_string(snap_lat.p95_us()) +
             " request_stop_us=" + std::to_string(stop_lat) +
             " join_us=" + std::to_string(join_lat));
    }

    // 3) 有界丢旧队列（depth=2，推 5 帧 -> 丢 3，队长 <= 2）。
    {
        std::vector<media::AiCameraSpec> cams = {{"CAM-01", true, "front"}};
        media::VisionWorkerConfig wc;
        wc.sched = {1, 2, 4, 85.0};   // interval=1：本用例验证队列丢旧，不做降频
        wc.cameras = cams;
        wc.semantic = semantic_cfg(th);
        auto det = std::make_shared<media::StubVisionDetector>(media::StubVisionDetector::Config{});
        media::VisionAiWorker worker(wc, det, nullptr, nullptr);   // 不起线程：纯入队
        // 从真实取流层逐帧取 5 个不同 seq（不是合成元数据），不入队消费。
        std::vector<std::uint64_t> pushed;
        std::uint64_t last = 0;
        const long long t0 = mono_ms();
        while (pushed.size() < 5 && mono_ms() - t0 < 3000) {
            src.stream->pump(20);
            media::FrameLease l = src.stream->latest_frame();
            if (!l.valid() || l.descriptor().seq == last) continue;
            last = l.descriptor().seq;
            pushed.push_back(last);
            worker.submit(l);
        }
        const std::size_t depth = worker.router().depth("CAM-01");
        const std::uint64_t dropped = worker.router().dropped("CAM-01");
        check(pushed.size() == 5 && depth <= 2 && dropped == 3, "bounded_drop_oldest",
              "pushed=" + std::to_string(pushed.size()) + " depth=" + std::to_string(depth) +
                  " dropped=" + std::to_string(dropped));
        // 消费一帧：保留的是最新两帧（丢最旧语义 -> 先出队的为倒数第二新）。
        media::AiFrame f1;
        media::AiFrame f2;
        const bool p1 = worker.router().pop("CAM-01", &f1);
        const bool p2 = worker.router().pop("CAM-01", &f2);
        check(p1 && p2 && pushed.size() >= 2 && f1.seq == pushed[pushed.size() - 2] &&
                  f2.seq == pushed.back(),
              "drop_oldest_keeps_newest",
              p1 && p2 ? ("popped=" + std::to_string(f1.seq) + "," + std::to_string(f2.seq) +
                         " last_pushed=" + std::to_string(pushed.back()))
                       : "pop failed");
    }

    // 4) 预算/无饥饿：4 路 budget=1，8 轮每路至少 1 次。
    {
        core::AiLaneScheduler sch({5, 2, 1, 85.0});
        std::vector<core::AiLaneDemand> demand;
        const char* cams[] = {"CAM-01", "CAM-05", "CAM-04", "CAM-06"};
        const core::AiProfile profs[] = {core::AiProfile::Front, core::AiProfile::WheelRail,
                                         core::AiProfile::Pantograph, core::AiProfile::CabDoor};
        for (int i = 0; i < 4; ++i) {
            core::AiLaneDemand d;
            d.camera_id = cams[i];
            d.profile = profs[i];
            d.has_frame = true;
            demand.push_back(d);
        }
        std::vector<std::pair<std::string, std::string>> zones = {{"thermal_zone0", "42000"}};
        const core::ThermalSample t = core::pick_thermal_sample(zones, 1);
        std::map<std::string, int> ran;
        for (int i = 0; i < 8; ++i) {
            core::AiScheduleRound r = sch.plan(demand, t);
            if (r.run.size() != 1) ++g_fail;
            ++ran[r.run[0]];
        }
        check(ran.size() == 4, "budget_fairness_all_lanes_ran", "4/4 within 8 rounds");
        bool twice = true;
        for (const auto& kv : ran) twice = twice && kv.second == 2;
        check(twice, "budget_fairness_balanced", "each lane 2 times");
    }

    // 5) 温度守卫：注入 > 阈值 -> 降载；注入样本标记 injected。
    {
        core::AiLaneScheduler sch({5, 2, 4, app.ai.temp_limit_c});
        std::vector<core::AiLaneDemand> demand;
        const char* cams[] = {"CAM-01", "CAM-05", "CAM-04", "CAM-06"};
        const core::AiProfile profs[] = {core::AiProfile::Front, core::AiProfile::WheelRail,
                                         core::AiProfile::Pantograph, core::AiProfile::CabDoor};
        for (int i = 0; i < 4; ++i) {
            core::AiLaneDemand d;
            d.camera_id = cams[i];
            d.profile = profs[i];
            d.has_frame = true;
            demand.push_back(d);
        }
        const core::ThermalSample hot =
            core::injected_thermal_sample(app.ai.temp_limit_c + 5.0, mono_ms());
        core::AiScheduleRound r = sch.plan(demand, hot);
        check(r.temperature_guard && r.budget_effective == 2,
              "temp_guard_halves_budget", "limit=" + std::to_string(app.ai.temp_limit_c) +
                                              " budget 4->" +
                                              std::to_string(r.budget_effective));
        check(r.thermal.injected && !r.thermal.measured(), "injected_temp_not_measured",
              core::thermal_state_token(r.thermal));
    }

    // 6) 真实实测温度源在场（sysfs）；不在场时显式不可用（不伪造）。
    {
        media::ThermalSensor ts;
        core::ThermalSample s = ts.read(mono_ms());
        emit("type=thermal state=" + std::string(core::thermal_state_token(s)) +
             " celsius=" + std::to_string(s.celsius) + " source=" + s.source +
             " reason=" + s.reason + " zones=" + std::to_string(ts.zones().size()));
        check(s.valid ? s.measured() : true, "thermal_source_reported_honestly",
              s.valid ? "measured from sysfs" : ("unavailable " + s.reason));
    }

    // 7) 不可用路：不产生事件、envelope 明确 unavailable；track_id 永不为 0。
    {
        std::vector<media::AiCameraSpec> cams = {{"CAM-01", true, "front"}, {"CAM-04", true, "pantograph"}};
        media::VisionWorkerConfig wc;
        wc.sched = {1, 2, 4, 85.0};
        wc.cameras = cams;
        wc.semantic = semantic_cfg(th);
        wc.rules = rule_cfg(th);
        media::StubVisionDetector::Config stubc;
        stubc.sleep_us = 0;
        stubc.available = true;
        auto det = std::make_shared<media::StubVisionDetector>(stubc);
        auto rules = std::make_shared<core::RuleEngine>(rule_cfg(th));
        media::VisionAiWorker worker(wc, det, nullptr, rules);   // 由 step() 驱动
        // 只给 CAM-01 推帧；CAM-04 保持空闲（无帧 -> 不推理、不产生事件）。
        for (int i = 0; i < 4; ++i) {
            worker.submit(lease);
            worker.step();
        }
        media::VisionWorkerSnapshot s = worker.snapshot();
        bool no_zero_track = true;
        for (const auto& l : s.lanes)
            for (const auto& b : l.boxes)
                if (b.track_id == 0) no_zero_track = false;
        check(no_zero_track, "track_id_never_zero", "boxes track_id != 0");
        bool idle_lane_quiet = true;
        for (const auto& l : s.lanes)
            if (l.camera_id == "CAM-04" && (l.inferred != 0 || l.events != 0)) idle_lane_quiet = false;
        check(idle_lane_quiet, "no_frame_lane_not_inferred", "CAM-04 consumed=0");
        check(s.lanes.size() == 2, "two_lanes_configured", std::to_string(s.lanes.size()));
        check(rules->events().size() >= 1, "rule_engine_receives_detections",
              "events=" + std::to_string(rules->events().size()) + " (stub, 非模型能力声明)");
    }

    src.stream->stop();
    const bool ok = g_fail == 0;
    emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") + " failures=" + std::to_string(g_fail));
    if (!out_json.empty()) {
        std::ofstream j(out_json, std::ios::trunc);
        if (j) {
            j << "{\"mode\":\"selftest\",\"failures\":" << g_fail
              << ",\"result\":\"" << (ok ? "PASS" : "FAIL") << "\"}\n";
        }
    }
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------- chain

struct ChainOptions {
    std::string config_dir;
    std::string models_dir;
    std::string source = "test";
    std::string file;
    double duration_s = 10.0;
    std::vector<std::string> lanes;      // 空 = 配置里所有 ai_enabled 路
    int budget = -1;                     // -1 = 用配置
    std::string out_json;
};

int run_chain(const ChainOptions& opt) {
    core::ConfigLoadResult loaded = core::load_config_dir(opt.config_dir);
    if (!loaded.ok) {
        emit("type=error config invalid: " + opt.config_dir);
        for (const auto& e : loaded.errors) emit("  " + e.str());
        return 1;
    }
    const core::AppConfig& app = loaded.settings.app;
    const core::ThresholdsConfig& th = loaded.settings.thresholds;
    const std::vector<core::CameraSlot> registry = core::build_registry(loaded.settings);

    std::vector<core::CameraSlot> selected;
    for (const auto& slot : registry) {
        if (!slot.ai_enabled || slot.ai_profile == core::AiProfile::None) continue;
        if (!opt.lanes.empty()) {
            if (std::find(opt.lanes.begin(), opt.lanes.end(), slot.id) == opt.lanes.end()) continue;
        }
        selected.push_back(slot);
    }
    if (selected.empty()) {
        emit("type=error no ai_enabled lane selected");
        return 2;
    }

    core::AiSchedulerConfig sched;
    sched.inference_interval = app.ai.inference_interval;
    sched.queue_depth = app.ai.queue_depth;
    sched.npu_budget = opt.budget > 0 ? opt.budget : app.ai.npu_budget;
    sched.temp_limit_c = app.ai.temp_limit_c;
    emit("type=config dir=" + opt.config_dir + " models_dir=" + opt.models_dir +
         " interval=" + std::to_string(sched.inference_interval) +
         " depth=" + std::to_string(sched.queue_depth) +
         " npu_budget=" + std::to_string(sched.npu_budget) +
         (opt.budget > 0 ? "(cli-override)" : "(config)") +
         " temp_limit_c=" + std::to_string(sched.temp_limit_c) +
         " confirm_frames=" + std::to_string(th.confirm_frames) +
         " confidence_min=" + std::to_string(th.confidence_min));

    // 真实检测器：模型名称来自配置，路径由 models 目录解析。
    media::RknnDetectorConfig dcfg;
    dcfg.models_dir = opt.models_dir;
    for (const auto& m : app.ai.models) {
        core::DeclaredModel d;
        d.profile = m.profile;
        d.name = m.name;
        d.version = m.version;
        dcfg.declared.push_back(d);
    }
    dcfg.rois = loaded.settings.roi;
    dcfg.detect.score_threshold = static_cast<float>(th.confidence_min);
    dcfg.detect.score_threshold_source = "app/config/thresholds.yaml:confidence_min";
    dcfg.detect.nms_iou = 0.45f;
    dcfg.detect.nms_iou_source = "detect default (0.45; 配置无独立键)";
    dcfg.detect.num_classes = 80;
    dcfg.detect.class_whitelist = {0};
    dcfg.seg.score_threshold = static_cast<float>(th.confidence_min);
    dcfg.seg.score_threshold_source = "app/config/thresholds.yaml:confidence_min";
    dcfg.seg.nms_iou = 0.45f;
    dcfg.seg.nms_iou_source = "seg default (0.45; 配置无独立键)";
    dcfg.seg.mask_threshold = 0.0f;
    dcfg.seg.mask_threshold_source = "historical decoder (>0)";
    dcfg.seg.crop_to_box = true;
    dcfg.two_stage_cache.enabled = false;
    dcfg.two_stage_cache.policy_note = "实时报警禁用跨帧缓存（默认关闭）";

    auto detector = std::make_shared<media::RknnVisionDetector>(dcfg);
    std::string load_err;
    const bool loaded_ok = detector->load(&load_err);
    emit("type=models " + detector->status_json());
    const auto profs = detector->profiles();
    for (const auto& p : profs) {
        emit("type=profile name=" + std::string(core::to_string(p.profile)) +
             " ready=" + env_bool(p.ready) + " reason=" + p.reason +
             " model=" + p.model_version);
    }

    // 工作线程（真实检测器）+ 规则引擎。
    media::VisionWorkerConfig wc;
    wc.sched = sched;
    for (const auto& slot : selected) wc.cameras.push_back(spec_of(slot));
    wc.semantic = semantic_cfg(th);
    wc.rules = rule_cfg(th);
    auto rules = std::make_shared<core::RuleEngine>(rule_cfg(th));
    auto thermal = std::make_shared<media::ThermalSensor>();
    media::VisionAiWorker worker(wc, detector, thermal, rules);

    // 取流源（file/test；真实 RTSP 属 ENV-D，不在本轮）。
    const std::string session = core::make_session_id();
    core::TimeBase tb;
    tb.calibrate(core::TimeBase::system_now_ms(), core::TimeBase::steady_now_us());
    std::vector<std::unique_ptr<LaneSource>> lanes;
    for (const auto& slot : selected) {
        media::CameraStreamConfig scfg;
        scfg.camera_id = slot.id;
        scfg.session_id = session;
        scfg.timebase = &tb;
        if (opt.source == "file") {
            scfg.source = media::SourceKind::File;
            scfg.file = opt.file;
        } else {
            scfg.source = media::SourceKind::Test;
            scfg.test_width = 1280;
            scfg.test_height = 720;
            scfg.test_fps = 25;
        }
        scfg.ai_queue_depth = sched.queue_depth;
        auto ls = std::unique_ptr<LaneSource>(new LaneSource());
        std::string serr;
        if (!start_stream(scfg, ls.get(), &serr)) {
            emit("type=error stream_start lane=" + slot.id + " err=" + serr);
            return 3;
        }
        emit("type=source lane=" + slot.id + " source=" + opt.source +
             " profile=" + core::to_string(slot.ai_profile));
        lanes.push_back(std::move(ls));
    }

    worker.start();
    const long long t0 = mono_ms();
    long long last_trace = 0;
    long long submitted = 0;
    long long lease_missing = 0;
    std::vector<std::uint64_t> last_seq(lanes.size(), 0);
    while (true) {
        const long long elapsed = mono_ms() - t0;
        if (elapsed >= static_cast<long long>(opt.duration_s * 1000)) break;
        for (std::size_t i = 0; i < lanes.size(); ++i) {
            lanes[i]->stream->pump(10);
            media::FrameLease l = lanes[i]->stream->latest_frame();
            if (!l.valid()) {
                ++lease_missing;
                continue;
            }
            if (l.descriptor().seq == last_seq[i]) continue;   // 同一帧不重复派发
            last_seq[i] = l.descriptor().seq;
            worker.submit(l);
            ++submitted;
        }
        if (elapsed - last_trace >= 1000) {
            last_trace = elapsed;
            const media::VisionWorkerSnapshot s = worker.snapshot();
            emit("type=trace t_ms=" + std::to_string(elapsed) +
                 " submitted=" + std::to_string(submitted) +
                 " steps=" + std::to_string(s.steps) +
                 " infer_calls=" + std::to_string(s.infer_calls) +
                 " events=" + std::to_string(s.events) +
                 " thermal=" + std::string(core::thermal_state_token(s.thermal)) +
                 " temp_c=" + std::to_string(s.thermal.celsius) +
                 " guard=" + env_bool(s.last_round.temperature_guard));
        }
    }

    const media::VisionWorkerSnapshot online = worker.snapshot();
    emit("type=ai-status running=" + core::ai_status_text(online.ai, mono_ms()) +
         " worker_alive=" + env_bool(online.worker_alive));

    // 停止：request_stop 立即生效（AI 不可用），再 join。
    const long long stop_t0 = mono_ms();
    worker.request_stop();
    const media::VisionWorkerSnapshot stopping = worker.snapshot();
    const long long request_us = (mono_ms() - stop_t0) * 1000;
    worker.stop();
    for (auto& l : lanes) l->stream->stop();
    const media::VisionWorkerSnapshot stopped = worker.snapshot();
    const std::string stopped_text = core::ai_status_text(stopped.ai, mono_ms());
    emit("type=stop request_stop_us=" + std::to_string(request_us) + " status=" + stopped_text);

    // 每路结果。
    long long published = 0;
    long long consumed = 0;
    long long unavailable = 0;
    long long events = 0;
    for (const auto& l : stopped.lanes) {
        published += l.published;
        consumed += l.consumed;
        unavailable += l.unavailable;
        events += l.events;
        std::string reasons;
        for (const auto& kv : l.unavailable_by_reason) {
            if (!reasons.empty()) reasons += ",";
            reasons += kv.first + ":" + std::to_string(kv.second);
        }
        emit("type=lane camera=" + l.camera_id + " profile=" + core::to_string(l.profile) +
             " ready=" + env_bool(l.ready) + " reason=" + l.unavailable_reason +
             " model=" + l.model_version + " consumed=" + std::to_string(l.consumed) +
             " inferred=" + std::to_string(l.inferred) +
             " unavailable=" + std::to_string(l.unavailable) +
             " unavailable_by_reason=" + (reasons.empty() ? "none" : reasons) +
             " last_unavailable_reason=" + l.last_unavailable_reason +
             " last_unavailable_detail=" + l.last_unavailable_detail +
             " published=" + std::to_string(l.published) +
             " dropped=" + std::to_string(l.dropped) +
             " skipped_interval=" + std::to_string(l.skipped_interval) +
             " queue_depth=" + std::to_string(l.queue_depth) +
             " last_infer_us=" + std::to_string(l.last_infer_us) +
             " latency_ms=" + std::to_string(l.latency_ms) +
             " fps=" + std::to_string(l.fps) + " tracks=" + std::to_string(l.tracks) +
             " events=" + std::to_string(l.events) +
             " env_ready=" + std::to_string(l.envelope_stats.accepted) +
             " env_unavailable=" + std::to_string(l.envelope_stats.unavailable) +
             " env_queue_dropped=" + std::to_string(l.envelope_stats.queue_dropped));
        if (!l.envelope_summary.empty())
            emit("type=envelope camera=" + l.camera_id + " json=" + l.envelope_summary);
    }
    emit("type=rule events_total=" + std::to_string(rules->events().size()) +
         " events_new=" + std::to_string(events));
    emit("type=summary submitted=" + std::to_string(submitted) +
         " consumed=" + std::to_string(consumed) +
         " published=" + std::to_string(published) +
         " unavailable=" + std::to_string(unavailable) +
         " round=" + std::to_string(stopped.last_round.round) +
         " budget=" + std::to_string(stopped.last_round.budget_effective) +
         " thermal_guard_rounds=" + std::to_string(stopped.thermal_guard_rounds) +
         " thermal_unknown_rounds=" + std::to_string(stopped.thermal_unknown_rounds) +
         " thermal=" + core::thermal_sample_json(stopped.thermal));

    // 验收判据（只陈述本轮实测范围）。
    check(loaded_ok, "chain_models_ready", load_err);
    check(submitted > 0 && consumed > 0, "chain_frames_dispatched_and_consumed",
          "submitted=" + std::to_string(submitted) + " consumed=" + std::to_string(consumed));
    check(published > 0, "chain_envelopes_published", std::to_string(published));
    check(!stopping.worker_alive && stopped_text == "AI 不可用", "chain_stop_ai_unavailable",
          stopped_text);
    check(stopped.steps > 0 && stopped.last_round.round > 0, "chain_scheduler_rounds",
          "rounds=" + std::to_string(stopped.last_round.round));
    check(stopped.last_round.budget_effective <= sched.npu_budget, "chain_budget_respected",
          std::to_string(stopped.last_round.budget_effective) + "<=" +
              std::to_string(sched.npu_budget));
    check(!stopped.last_round.thermal_known || stopped.thermal.valid, "chain_thermal_reported",
          core::thermal_sample_json(stopped.thermal));
    {
        // 守卫判定与实测温度一致（不是拿模拟值冒充）：guard == (measured > limit)
        const bool measured_guard = stopped.thermal.valid &&
                                    stopped.thermal.celsius > sched.temp_limit_c;
        check(!stopped.thermal.valid || measured_guard == stopped.last_round.temperature_guard,
              "chain_guard_matches_measured_temp",
              "measured=" + std::to_string(stopped.thermal.celsius) + " limit=" +
                  std::to_string(sched.temp_limit_c) + " guard=" +
                  env_bool(stopped.last_round.temperature_guard));
    }
    check(lease_missing < static_cast<long long>(submitted), "chain_lease_available",
          "missing_polls=" + std::to_string(lease_missing));

    const bool ok = g_fail == 0;
    emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") + " failures=" + std::to_string(g_fail));
    if (!opt.out_json.empty()) {
        std::ofstream j(opt.out_json, std::ios::trunc);
        if (j) {
            j << "{\"mode\":\"chain\""
              << ",\"source\":\"" << opt.source << "\""
              << ",\"duration_s\":" << opt.duration_s
              << ",\"budget_configured\":" << sched.npu_budget
              << ",\"budget_effective\":" << stopped.last_round.budget_effective
              << ",\"submitted\":" << submitted
              << ",\"consumed\":" << consumed
              << ",\"published\":" << published
              << ",\"unavailable\":" << unavailable
              << ",\"events\":" << rules->events().size()
              << ",\"thermal\":" << core::thermal_sample_json(stopped.thermal)
              << ",\"thermal_guard_rounds\":" << stopped.thermal_guard_rounds
              << ",\"steps\":" << stopped.steps
              << ",\"infer_calls\":" << stopped.infer_calls
              << ",\"detector\":" << detector->status_json()
              << ",\"lanes\":[";
            for (std::size_t i = 0; i < stopped.lanes.size(); ++i) {
                const auto& l = stopped.lanes[i];
                if (i) j << ",";
                j << "{\"camera\":\"" << l.camera_id << "\""
                  << ",\"profile\":\"" << core::to_string(l.profile) << "\""
                  << ",\"ready\":" << (l.ready ? "true" : "false")
                  << ",\"reason\":\"" << l.unavailable_reason << "\""
                  << ",\"model\":\"" << l.model_version << "\""
                  << ",\"consumed\":" << l.consumed
                  << ",\"inferred\":" << l.inferred
                  << ",\"unavailable\":" << l.unavailable
                  << ",\"published\":" << l.published
                  << ",\"dropped\":" << l.dropped
                  << ",\"last_infer_us\":" << l.last_infer_us
                  << ",\"latency_ms\":" << l.latency_ms
                  << ",\"fps\":" << l.fps
                  << ",\"unavailable_reason\":\"" << l.last_unavailable_reason << "\""
                  << ",\"unavailable_detail\":\"" << l.last_unavailable_detail << "\""
                  << ",\"events\":" << l.events
                  << ",\"last_round\":" << core::schedule_round_json(stopped.last_round)
                  << "}";
            }
            j << "],\"result\":\"" << (ok ? "PASS" : "FAIL") << "\""
              << ",\"failures\":" << g_fail << "}\n";
        }
    }
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string config = RAILHMI_CONFIG_DIR;
    std::string models_dir = "../models";
    std::string mode = "selftest";
    std::string out;
    std::string json;
    ChainOptions chain;
    bool selftest = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) {
            if (i + 1 < argc) *d = argv[++i];
        };
        if (a == "--config") next(&config);
        else if (a == "--models-dir") next(&models_dir);
        else if (a == "--mode") next(&mode);
        else if (a == "--source") next(&chain.source);
        else if (a == "--file") next(&chain.file);
        else if (a == "--duration") { std::string v; next(&v); chain.duration_s = std::atof(v.c_str()); }
        else if (a == "--lanes") {
            std::string v;
            next(&v);
            std::size_t p = 0;
            while (p <= v.size()) {
                const std::size_t c = v.find(',', p);
                const std::string item = v.substr(p, c == std::string::npos ? std::string::npos : c - p);
                if (!item.empty()) chain.lanes.push_back(item);
                if (c == std::string::npos) break;
                p = c + 1;
            }
        }
        else if (a == "--budget") { std::string v; next(&v); chain.budget = std::atoi(v.c_str()); }
        else if (a == "--selftest") selftest = true;
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--help" || a == "-h") {
            std::printf(
                "usage: railhmi-vision-pipeline [--selftest] [--mode selftest|chain|all] [--config DIR] "
                "[--models-dir DIR] [--source test|file] [--file F] [--duration S] "
                "[--lanes CAM-01,...] [--budget N] [--out LOG] [--json FILE]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }

    g_fo.open(out, std::ios::trunc);
    chain.config_dir = config;
    chain.models_dir = models_dir;
    chain.out_json = json;

    int rc = 0;
    if (selftest || mode == "selftest" || mode == "all") {
        rc = run_selftest(config, json);
        if (rc != 0) return rc;
    }
    if (!selftest && (mode == "chain" || mode == "all" || mode.empty())) {
        rc = run_chain(chain);
    }
    return rc;
}
