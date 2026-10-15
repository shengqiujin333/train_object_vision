#pragma once

// railhmi-media: 推理工作线程（ITEM-017）。
// 独立线程消费每路有界丢旧帧队列，按配置帧间隔调用可插拔 Detector，产出 DetectionResult，
// 并维护 railhmi-core::AiRuntime 遥测（fps/耗时/最近结果）。start/stop 明确 join，退出清理干净。
// Detector 由视觉/模型职责提供；本层提供 SimDetector（替代输入，非 RKNN）用于无模型时验证
// 线程/调度/遥测与“停止后 AI 不可用”语义。target 专用。

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "railhmi/core/inference.h"

namespace railhmi {
namespace media {

// 检测器接口：应用消费模型版本/框/ROI/置信度/耗时，不定义精度。
class Detector {
public:
    virtual ~Detector() = default;
    virtual std::string model_version(railhmi::core::AiProfile profile) const = 0;
    virtual bool ready() const = 0;
    virtual railhmi::core::DetectionResult infer(const railhmi::core::FrameRef& frame,
                                                 railhmi::core::AiProfile profile) = 0;
};

// 确定性模拟检测器：按 profile 返回固定框/置信度（替代输入；不加载 RKNN 模型）。
class SimDetector : public Detector {
public:
    explicit SimDetector(std::map<railhmi::core::AiProfile, std::string> models);
    std::string model_version(railhmi::core::AiProfile profile) const override;
    bool ready() const override { return true; }
    railhmi::core::DetectionResult infer(const railhmi::core::FrameRef& frame,
                                         railhmi::core::AiProfile profile) override;

private:
    std::map<railhmi::core::AiProfile, std::string> models_;
};

struct AiWorkerConfig {
    railhmi::core::AiSchedulerConfig sched;
    std::vector<railhmi::core::CameraSlot> cameras;   // 提供 id/profile/ai_enabled
    double temp_c = 60.0;
};

// 推理工作线程：自驱动 tick（可外部 submit）。tick 推进内部时钟、入队并推理到期帧。
class AiWorker {
public:
    AiWorker(AiWorkerConfig cfg, std::shared_ptr<Detector> detector);
    ~AiWorker();

    AiWorker(const AiWorker&) = delete;
    AiWorker& operator=(const AiWorker&) = delete;

    void start();                        // 启动工作线程
    void stop();                         // 停止并 join（幂等）
    bool running() const { return running_.load(); }

    // 由工作线程周期调用；也可单独测试。返回本轮推理路数。
    int tick(long long step_ms);
    // 外部推帧（真实接入时由取流层调用）。
    void submit(const railhmi::core::FrameRef& frame);

    railhmi::core::AiRuntime snapshot() const;     // 线程安全遥测
    void set_temp(double c) { temp_c_ = c; }

private:
    void loop();

    AiWorkerConfig cfg_;
    std::shared_ptr<Detector> detector_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    mutable std::mutex mu_;
    railhmi::core::AiRuntime rt_;
    std::vector<railhmi::core::BoundedFrameQueue> queues_;
    std::vector<long long> inferred_;
    long long clock_ms_ = 0;
    uint64_t seq_ = 0;
    long long started_ms_ = 0;
    double temp_c_ = 60.0;
};

}  // namespace media
}  // namespace railhmi
