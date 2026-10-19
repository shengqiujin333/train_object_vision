// railhmi-media: 真实检测器推理工作线程与调度实现（视觉推理 ITEM-009）。
//
// 锁纪律（TC-T-DISP-03）：推理在 `mu_` 之外执行；`submit()` / `snapshot()` 只做短临界区
// 更新/拷贝；`request_stop()` 只改原子标志。`stop()` 的 join 最多等待当前一次推理返回。

#include "railhmi/media/vision_ai_worker.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace railhmi {
namespace media {

namespace {

long long mono_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

long long mono_ms() { return mono_us() / 1000; }

}  // namespace

struct VisionAiWorker::Lane {
    std::string camera_id;
    core::AiProfile profile = core::AiProfile::None;
    std::unique_ptr<core::EnvelopePublisher> publisher;
    std::unique_ptr<core::SemanticAdapter> adapter;
    core::LightweightTracker tracker;

    // 遥测（受 VisionAiWorker::mu_ 保护）。
    std::string model_version;
    long long consumed = 0;
    long long inferred = 0;
    long long unavailable = 0;
    long long published = 0;
    long long last_infer_us = 0;
    int latency_ms = 0;
    long long last_result_mono_ms = -1;
    long long first_result_mono_ms = -1;
    long long events = 0;
    std::string envelope_summary;
    std::map<std::string, long long> unavailable_by_reason;
    std::string last_reason;
    std::string last_detail;
    std::vector<core::EnvelopeBox> boxes;
    core::EnvelopeStats stats;
    core::SemanticStats sem;
};

// ---------------------------------------------------------------- StubVisionDetector

StubVisionDetector::StubVisionDetector(Config cfg) : cfg_(std::move(cfg)) {}

std::string StubVisionDetector::model_version(core::AiProfile) const { return cfg_.model_version; }

std::string StubVisionDetector::unavailable_reason(core::AiProfile) const {
    return cfg_.available ? std::string() : cfg_.reason;
}

VisionInferOutcome StubVisionDetector::infer(const AiFrame& frame, core::AiProfile p) {
    calls_.fetch_add(1);
    const long long t0 = mono_us();
    if (cfg_.sleep_us > 0)
        std::this_thread::sleep_for(std::chrono::microseconds(cfg_.sleep_us));

    VisionInferOutcome out;
    out.envelope.frame.session_id = frame.session_id;
    out.envelope.frame.camera_id = frame.camera_id;
    out.envelope.frame.generation = frame.generation;
    out.envelope.frame.seq = frame.seq;
    out.envelope.profile = p;
    out.envelope.capture_mono_us = frame.capture_mono_us;
    out.envelope.model_version = cfg_.model_version;

    if (!cfg_.available) {
        out.available = false;
        out.reason = cfg_.reason;
        out.envelope.availability = core::AiAvailability::Unavailable;
        out.envelope.reason = cfg_.reason;
        out.envelope.result_mono_us = mono_us();
        out.envelope.latency_ms =
            core::latency_ms_of(out.envelope.capture_mono_us, out.envelope.result_mono_us);
        out.infer_us = mono_us() - t0;
        return out;
    }

    std::vector<core::DetectBox> boxes;
    for (int i = 0; i < cfg_.boxes; ++i) {
        core::DetectBox b;
        b.x = 0.40f + 0.05f * static_cast<float>(i);
        b.y = 0.50f;
        b.w = 0.10f;
        b.h = 0.20f;
        b.score = cfg_.confidence;
        b.class_id = 0;
        b.label = "person";
        boxes.push_back(b);
    }
    const long long result_us = mono_us();
    core::EnvelopeBuildInput in;
    in.frame = out.envelope.frame;
    in.profile = p;
    in.model_version = cfg_.model_version;
    in.capture_mono_us = frame.capture_mono_us;
    in.result_mono_us = result_us;
    in.detect_ready = true;
    in.requires_segmentation = false;
    in.boxes = &boxes;
    // 替代检测器：框标记为“在 ROI 内”，以便自检能验证规则下发链路（非能力声明）。
    in.in_roi_flags.assign(boxes.size(), 1);
    out.envelope = core::build_envelope(in);
    out.available = out.envelope.ready();
    out.reason = out.envelope.reason;
    out.infer_us = result_us - t0;
    return out;
}

// ---------------------------------------------------------------- VisionAiWorker

VisionAiWorker::VisionAiWorker(VisionWorkerConfig cfg, std::shared_ptr<VisionDetector> detector,
                               std::shared_ptr<ThermalSensor> thermal,
                               std::shared_ptr<core::RuleEngine> rules)
    : cfg_(std::move(cfg)),
      detector_(std::move(detector)),
      thermal_(std::move(thermal)),
      rules_(std::move(rules)),
      router_(cfg_.cameras, cfg_.sched.inference_interval, cfg_.sched.queue_depth),
      scheduler_(cfg_.sched) {
    if (cfg_.envelope_queue_depth < 1) cfg_.envelope_queue_depth = 1;
    for (const auto& spec : cfg_.cameras) {
        core::AiProfile p = core::AiProfile::None;
        core::parse_ai_profile(spec.ai_profile, &p);
        if (!spec.ai_enabled || p == core::AiProfile::None) continue;
        auto lane = std::unique_ptr<Lane>(new Lane());
        lane->camera_id = spec.camera_id;
        lane->profile = p;
        lane->publisher.reset(new core::EnvelopePublisher("", cfg_.envelope_queue_depth));
        core::SemanticConfig sc = cfg_.semantic;
        sc.profile = p;
        lane->adapter.reset(new core::SemanticAdapter(sc));
        lane->tracker = core::LightweightTracker(cfg_.tracker);
        lane->model_version = detector_ ? detector_->model_version(p) : std::string();
        lanes_.push_back(std::move(lane));
    }
}

VisionAiWorker::~VisionAiWorker() { stop(); }

void VisionAiWorker::start() {
    if (running_.load()) return;
    running_.store(true);
    {
        std::lock_guard<std::mutex> lk(mu_);
        started_mono_ms_ = mono_ms();
    }
    worker_alive_.store(true);
    thread_ = std::thread(&VisionAiWorker::loop, this);
}

void VisionAiWorker::request_stop() {
    running_.store(false);
    // 立即对外表达“推理服务已停”（AI 不可用语义不必等到线程退出）。
    worker_alive_.store(false);
}

void VisionAiWorker::stop() {
    request_stop();
    if (thread_.joinable()) thread_.join();
    worker_alive_.store(false);
}

void VisionAiWorker::loop() {
    while (running_.load()) {
        step();
        if (cfg_.loop_sleep_ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.loop_sleep_ms));
    }
}

bool VisionAiWorker::submit(const FrameLease& lease) {
    if (!lease.valid()) return false;
    const FrameDescriptor& d = lease.descriptor();
    {
        std::lock_guard<std::mutex> lk(mu_);
        ++submit_calls_;
    }
    const std::uint64_t before = router_.dropped(d.camera_id);
    router_.on_frame(lease);
    const std::uint64_t after = router_.dropped(d.camera_id);
    if (after > before) {
        std::lock_guard<std::mutex> lk(mu_);
        submit_dropped_ += static_cast<long long>(after - before);
    }
    return router_.enabled(d.camera_id);
}

void VisionAiWorker::submit_frame(const AiFrame& frame) {
    if (!frame.lease.valid()) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        ++submit_calls_;
    }
    const std::uint64_t before = router_.dropped(frame.camera_id);
    router_.on_frame(frame.lease);
    const std::uint64_t after = router_.dropped(frame.camera_id);
    if (after > before) {
        std::lock_guard<std::mutex> lk(mu_);
        submit_dropped_ += static_cast<long long>(after - before);
    }
}

int VisionAiWorker::step() {
    if (lanes_.empty()) return 0;
    const long long now_us = mono_us();
    const long long now_ms = now_us / 1000;

    // 1) 温度样本（锁外读 sysfs，最多 max_zones 次打开）。
    core::ThermalSample thermal;
    if (thermal_) {
        thermal = thermal_->read(now_ms);
    } else {
        thermal.reason = core::thermal_reason::kMissing();
    }

    // 2) 需求快照：只问“该路队列是否有帧”，不持锁访问像素。
    std::vector<core::AiLaneDemand> demand;
    demand.reserve(lanes_.size());
    for (const auto& lane : lanes_) {
        core::AiLaneDemand d;
        d.camera_id = lane->camera_id;
        d.profile = lane->profile;
        d.has_frame = router_.depth(lane->camera_id) > 0;
        // ITEM-010：职责未就绪（无核验模型）的路不占路数预算，但仍以“不可用”语义消费/发布。
        d.duty_ready = detector_ ? detector_->ready(lane->profile) : false;
        demand.push_back(d);
    }

    // 3) 调度（plan_inference 预算/温度守卫 + 无饥饿轮转）。
    const core::AiScheduleRound round = scheduler_.plan(demand, thermal);

    // 4) 推理（锁外）。每路每轮最多一次。
    int ran = 0;
    auto process_lane = [&](const std::string& cam) -> bool {
        Lane* lane = nullptr;
        for (auto& l : lanes_)
            if (l->camera_id == cam) lane = l.get();
        if (!lane) return false;
        AiFrame frame;
        if (!router_.pop(cam, &frame)) return false;
        if (!frame.lease.valid()) return false;

        VisionInferOutcome out = detector_->infer(frame, lane->profile);  // 不持 mu_

        // 4a) 跟踪（工作线程独占；不可用帧只推进 misses，不伪造 id）。
        const long long ts_ms = mono_us() / 1000;
        if (out.envelope.availability == core::AiAvailability::Ready) {
            auto tracked = lane->tracker.update(out.envelope.boxes, ts_ms);
            const std::size_t n = std::min(tracked.size(), out.envelope.boxes.size());
            for (std::size_t k = 0; k < n; ++k) {
                out.envelope.boxes[k].track_id = tracked[k].track_id;
                // track 分配失败（超 max_tracks）时保持 null(-1)，不能用 0 冒充。
            }
        } else {
            lane->tracker.tick_missing(ts_ms);
        }

        // 4b) 发布（单生产者 = 本线程；有界丢最旧）。
        std::string reject;
        const bool accepted = lane->publisher->submit(out.envelope, &reject);
        core::InferenceEnvelope drained;
        long long drained_n = 0;
        while (lane->publisher->pop(&drained)) ++drained_n;

        // 4c) 语义适配 + 规则（不可用 envelope 不产生任何报警）。
        core::SemanticResult sem;
        std::vector<core::AlarmEvent> events;
        if (lane->adapter) {
            events = lane->adapter->adapt_and_dispatch(out.envelope, ts_ms, rules_.get(), &sem);
        }

        // 4d) 遥测（短临界区）。
        {
            std::lock_guard<std::mutex> lk(mu_);
            ++lane->consumed;
            ++lane->inferred;
            if (!out.available) {
                ++lane->unavailable;
                ++lane->unavailable_by_reason[out.reason];
                lane->last_reason = out.reason;
                lane->last_detail = out.detail;
            }
            if (accepted) ++lane->published;
            lane->last_infer_us = out.infer_us;
            lane->latency_ms = out.envelope.latency_ms;
            lane->last_result_mono_ms = ts_ms;
            if (lane->first_result_mono_ms < 0) lane->first_result_mono_ms = ts_ms;
            lane->events += static_cast<long long>(events.size());
            events_ += static_cast<long long>(events.size());
            lane->boxes = out.envelope.boxes;
            lane->envelope_summary = core::envelope_json(out.envelope);
            lane->stats = lane->publisher->stats();
            lane->sem = sem.stats;
            ++infer_calls_;
            if (!out.available) ++infer_errors_;
            (void)drained_n;
        }
        return true;
    };

    // 4d) 预算内路：正常推理。
    for (const auto& cam : round.run)
        if (process_lane(cam)) ++ran;

    // 4e) 职责不可用路（ITEM-010）：不占路数预算，仍消费一帧以保持
    //     “unavailable + reason + 空框”语义与队列有界（这些路不触发 NPU 计算）。
    for (const auto& cam : round.unavailable) process_lane(cam);

    // 5) 本轮决策与温度入账。
    {
        std::lock_guard<std::mutex> lk(mu_);
        ++steps_;
        thermal_sample = thermal;
        last_round_ = round;
        if (round.temperature_guard) ++thermal_guard_rounds_;
        if (!round.thermal_known) ++thermal_unknown_rounds_;
    }
    return ran;
}

VisionWorkerSnapshot VisionAiWorker::snapshot() const {
    VisionWorkerSnapshot s;
    std::lock_guard<std::mutex> lk(mu_);
    s.worker_alive = worker_alive_.load();
    s.running = running_.load();
    s.engine = detector_ ? detector_->engine() : "none";
    s.steps = steps_;
    s.infer_calls = infer_calls_;
    s.infer_errors = infer_errors_;
    s.events = events_;
    s.thermal_guard_rounds = thermal_guard_rounds_;
    s.thermal_unknown_rounds = thermal_unknown_rounds_;
    s.submit_calls = submit_calls_;
    s.submit_dropped = submit_dropped_;
    s.thermal = thermal_sample;
    s.last_round = last_round_;
    const long long now_ms = mono_ms();
    for (const auto& lane : lanes_) {
        VisionLaneSnapshot l;
        l.camera_id = lane->camera_id;
        l.profile = lane->profile;
        l.model_version = lane->model_version;
        l.ready = detector_ && detector_->ready(lane->profile) && lane->consumed > 0;
        l.unavailable_reason = (detector_ && !detector_->ready(lane->profile))
                                   ? detector_->unavailable_reason(lane->profile)
                                   : std::string();
        l.consumed = lane->consumed;
        l.inferred = lane->inferred;
        l.unavailable = lane->unavailable;
        l.published = lane->published;
        l.last_infer_us = lane->last_infer_us;
        l.latency_ms = lane->latency_ms;
        l.last_result_mono_ms = lane->last_result_mono_ms;
        l.events = lane->events;
        l.tracks = lane->tracker.size();
        l.boxes = lane->boxes;
        l.envelope_summary = lane->envelope_summary;
        l.unavailable_by_reason = lane->unavailable_by_reason;
        l.last_unavailable_reason = lane->last_reason;
        l.last_unavailable_detail = lane->last_detail;
        l.envelope_stats = lane->stats;
        l.semantic_stats = lane->sem;
        l.queue_depth = router_.depth(lane->camera_id);
        l.dropped = static_cast<long long>(router_.dropped(lane->camera_id));
        l.skipped_interval =
            static_cast<long long>(router_.skipped_interval(lane->camera_id));
        l.rejected_disabled =
            static_cast<long long>(router_.rejected_disabled(lane->camera_id));
        if (lane->inferred > 1 && lane->first_result_mono_ms > 0)
            l.fps = static_cast<double>(lane->inferred - 1) * 1000.0 /
                    (static_cast<double>(std::max<long long>(1, lane->last_result_mono_ms -
                                                                    lane->first_result_mono_ms + 1)));
        s.lanes.push_back(std::move(l));
    }
    // 标准 AiRuntime 遥测（P5/状态区复用；停止后 worker_alive=false -> “AI 不可用”）。
    s.ai.worker_alive = s.worker_alive;
    s.ai.engine = s.engine;
    for (const auto& l : s.lanes) {
        core::AiChannelRuntime c;
        c.camera_id = l.camera_id;
        c.profile = l.profile;
        c.model_version = l.model_version;
        c.fps = l.fps;
        c.latency_ms = l.latency_ms;
        c.last_result_ms = l.last_result_mono_ms;
        c.dropped = l.dropped;
        c.queue_depth = static_cast<int>(l.queue_depth);
        s.ai.channels.push_back(c);
    }
    (void)now_ms;
    return s;
}

}  // namespace media
}  // namespace railhmi
