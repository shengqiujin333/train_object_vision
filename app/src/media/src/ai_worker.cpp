#include "railhmi/media/ai_worker.h"

#include <chrono>

namespace railhmi {
namespace media {

SimDetector::SimDetector(std::map<railhmi::core::AiProfile, std::string> models)
    : models_(std::move(models)) {}

std::string SimDetector::model_version(railhmi::core::AiProfile profile) const {
    auto it = models_.find(profile);
    return it != models_.end() ? it->second : std::string();
}

railhmi::core::DetectionResult SimDetector::infer(const railhmi::core::FrameRef& frame,
                                                  railhmi::core::AiProfile profile) {
    using namespace railhmi::core;
    DetectionResult r;
    r.camera_id = frame.camera_id;
    r.profile = profile;
    r.ts_ms = frame.ts_ms;
    r.model_version = model_version(profile);
    r.latency_ms = 30 + ai_priority(profile) * 3;
    DetectionBox b;
    b.x = 0.40f;
    b.y = 0.50f;
    b.w = 0.10f;
    b.h = 0.20f;
    b.confidence = 0.80f;
    b.class_id = 0;
    b.label = "sim-target";
    b.in_roi = true;
    b.distance_m = 8.0f + static_cast<float>(ai_priority(profile));   // 确定性距离（替代输入）
    r.boxes.push_back(b);
    r.max_confidence = b.confidence;
    return r;
}

AiWorker::AiWorker(AiWorkerConfig cfg, std::shared_ptr<Detector> detector)
    : cfg_(std::move(cfg)), detector_(std::move(detector)), temp_c_(cfg_.temp_c) {
    rt_.worker_alive = false;
    rt_.engine = "rknn";
    for (const auto& s : cfg_.cameras) {
        railhmi::core::AiChannelRuntime c;
        c.camera_id = s.id;
        c.profile = s.ai_profile;
        c.model_version = detector_ ? detector_->model_version(s.ai_profile) : std::string();
        rt_.channels.push_back(c);
        queues_.emplace_back(cfg_.sched.queue_depth);
        inferred_.push_back(0);
    }
}

AiWorker::~AiWorker() { stop(); }

void AiWorker::start() {
    if (running_.load()) return;
    running_.store(true);
    rt_.worker_alive = true;
    thread_ = std::thread(&AiWorker::loop, this);
}

void AiWorker::stop() {
    if (!running_.load() && !thread_.joinable()) {
        rt_.worker_alive = false;
        return;
    }
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    rt_.worker_alive = false;
}

void AiWorker::loop() {
    while (running_.load()) {
        tick(40);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
}

int AiWorker::tick(long long step_ms) {
    using namespace railhmi::core;
    std::lock_guard<std::mutex> lk(mu_);
    clock_ms_ += step_ms;
    if (started_ms_ == 0) started_ms_ = clock_ms_;
    ++seq_;
    int ran = 0;
    for (std::size_t i = 0; i < rt_.channels.size(); ++i) {
        AiChannelRuntime& c = rt_.channels[i];
        if (!ai_eligible(c.profile)) continue;
        c.last_frame_ms = clock_ms_;
        queues_[i].push(FrameRef{c.camera_id, seq_, clock_ms_});
        c.dropped = queues_[i].dropped();
        if (!frame_due(seq_, cfg_.sched.inference_interval)) continue;
        FrameRef f;
        if (!queues_[i].pop(&f)) continue;
        if (detector_ && detector_->ready()) {
            DetectionResult res = detector_->infer(f, c.profile);
            c.last_result_ms = clock_ms_;
            c.latency_ms = res.latency_ms;
            c.queue_depth = static_cast<int>(queues_[i].size());
            ++inferred_[i];
            ++ran;
        }
    }
    const double dur_s = (clock_ms_ - started_ms_) / 1000.0 + 0.001;
    for (std::size_t i = 0; i < rt_.channels.size(); ++i)
        rt_.channels[i].fps = inferred_[i] / dur_s;
    return ran;
}

void AiWorker::submit(const railhmi::core::FrameRef& frame) {
    std::lock_guard<std::mutex> lk(mu_);
    for (std::size_t i = 0; i < rt_.channels.size(); ++i)
        if (rt_.channels[i].camera_id == frame.camera_id) queues_[i].push(frame);
}

railhmi::core::AiRuntime AiWorker::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    return rt_;
}

}  // namespace media
}  // namespace railhmi
