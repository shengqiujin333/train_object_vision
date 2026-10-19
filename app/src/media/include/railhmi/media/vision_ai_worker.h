#pragma once

// railhmi-media: 真实检测器接入推理工作线程与调度（视觉推理 ITEM-009）。
//
// 本类是**生产推理工作线程**：消费 `AiFrameRouter`（ai_dispatcher 的带身份
// (session_id,camera_id,generation,seq) 有界丢旧队列），按 `core::AiLaneScheduler`
// 的路数预算与温度守卫决定每轮跑哪几路，用**真实检测器**（`VisionDetector`，像素来自
// `FrameLease`）推理，产出带身份的 `InferenceEnvelope` 并交给语义适配/规则引擎。
//
// 关键约束：
//   * **生产模式无自造帧**：帧只能由取流层 `submit(lease)` 进入；不存在内部 tick 造帧。
//   * **infer 不持调度锁**：推理在锁外执行；`submit`/`snapshot` 只持短锁更新状态，
//     `request_stop()` 立即返回并把 AI 置为不可用（`stop()` = request + join，最多等待
//     当前这一次推理完成）。
//   * **不可用 ≠ 安全**：检测器不可用/分割失败 -> `availability=unavailable` + reason + 空框，
//     与 `ready` + 空框在字段上可区分，不产生任何报警。
//   * **track_id 不为 0**：每路轻量跟踪器分配 id（>=1）；无法分配时为 -1（null）。
//   * **温度**：守卫使用 `ThermalSensor` 的实测样本；样本不可用时 `thermal_known=false`，
//     不宣称“温度正常”，注入样本显式标记 `injected`（不冒充实测）。

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "railhmi/core/inference.h"
#include "railhmi/core/rules.h"
#include "railhmi/core/vision_distance.h"        // ITEM-011 距离测量
#include "railhmi/core/vision_inference_envelope.h"
#include "railhmi/core/vision_seg_postprocess.h"        // SegResult（掩码所有权）
#include "railhmi/core/vision_semantic_adapter.h"
#include "railhmi/core/vision_tracker.h"
#include "railhmi/core/vision_worker_schedule.h"
#include "railhmi/media/ai_dispatcher.h"
#include "railhmi/media/frame_lease.h"
#include "railhmi/media/thermal_sensor.h"

namespace railhmi {
namespace media {

// 一次真实推理的输出。`seg` 持有掩码所有权：envelope.masks 的 plane 指针指向该对象，
// 因此 outcome（或其 seg 共享指针）必须在 envelope 使用期内存活。
struct VisionInferOutcome {
    bool available = false;
    std::string reason = "AI_UNAVAILABLE";     // envelope_reason/vision_reason token
    std::string detail;                        // 脱敏说明
    core::InferenceEnvelope envelope;
    std::shared_ptr<const core::SegResult> seg;
    long long infer_us = 0;                    // 纯推理+后处理耗时（不含排队）
    long long preprocess_us = 0;
};

// 真实检测器接口：只接受带身份的帧租约（像素唯一来源），不接受 FrameRef 元数据。
class VisionDetector {
public:
    virtual ~VisionDetector() = default;
    virtual std::string model_version(core::AiProfile p) const = 0;
    virtual bool ready(core::AiProfile p) const = 0;
    virtual std::string unavailable_reason(core::AiProfile p) const = 0;
    // 由工作线程单线程调用；实现可持有模型上下文。frame.lease 必须有效。
    virtual VisionInferOutcome infer(const AiFrame& frame, core::AiProfile p) = 0;
    // 证据用引擎标识（"rknn" / "stub"）。
    virtual std::string engine() const { return "rknn"; }
};

// 确定性替代检测器：用于线程/调度/发布链路的可测替身。
// **不是 RKNN、不加载模型**，不得用于任何能力/性能声明；`sleep_us` 用于模拟慢推理。
class StubVisionDetector : public VisionDetector {
public:
    struct Config {
        long long sleep_us = 0;          // 模拟单次推理耗时
        int boxes = 1;                   // 每次返回的框数（0 = ready 但空框）
        float confidence = 0.8f;
        bool available = true;           // false -> unavailable + reason
        std::string reason = "AI_UNAVAILABLE";
        std::string model_version = "stub-rknn-0";
    };

    explicit StubVisionDetector(Config cfg);

    std::string model_version(core::AiProfile p) const override;
    bool ready(core::AiProfile p) const override { return cfg_.available; }
    std::string unavailable_reason(core::AiProfile p) const override;
    VisionInferOutcome infer(const AiFrame& frame, core::AiProfile p) override;
    std::string engine() const override { return "stub"; }
    long long calls() const { return calls_.load(); }

private:
    Config cfg_;
    std::atomic<long long> calls_{0};
};

struct VisionWorkerConfig {
    core::AiSchedulerConfig sched;
    std::vector<AiCameraSpec> cameras;            // 来自 cameras.yaml/app.ai（ai_enabled/ai_profile）
    core::SemanticConfig semantic;                // 白名单/阈值（confidence_min 来自 thresholds.yaml）
    core::TrackerConfig tracker;
    core::RuleConfig rules;
    int loop_sleep_ms = 5;
    long long thermal_max_age_ms = 2000;
    int envelope_queue_depth = 2;
    // ITEM-011：相机标定（含变焦档位）与“当前档位”。未标定/档位未知时距离恒 -1。
    railhmi::core::DistanceCalibrationSet calibrations;
    std::map<std::string, std::string> camera_zoom;   // camera_id -> 当前变焦档位（缺省=未知）
    // ITEM-012：P5 掩码叠加开关开启时才抓取“最近一帧掩码”（避免常态拷贝 ~1MB/帧）。
    bool capture_latest_mask = false;
};

// 每路遥测（P5/证据口径；不伪造未测数值）。
struct VisionLaneSnapshot {
    std::string camera_id;
    core::AiProfile profile = core::AiProfile::None;
    bool ready = false;
    std::string unavailable_reason;
    std::string model_version;
    long long consumed = 0;        // 出队帧数（真正进入推理）
    long long inferred = 0;        // 完成推理次数（含不可用结果）
    long long unavailable = 0;     // 不可用结果数
    long long published = 0;       // 入发布队列数
    long long dropped = 0;         // 有界队列丢旧帧（router）
    long long skipped_interval = 0;
    long long rejected_disabled = 0;
    long long last_infer_us = 0;
    int latency_ms = 0;            // 端到端（含排队），来自 envelope
    double fps = 0.0;
    long long last_result_mono_ms = -1;
    std::size_t queue_depth = 0;
    long long events = 0;          // 规则引擎新开事件数（该路）
    // ITEM-011 距离测量（-1/未知占位；分类计数可观察）
    long long distance_valid = 0;
    long long distance_unknown = 0;
    std::map<std::string, long long> distance_unknown_by_reason;
    float last_distance_m = -1.0f;
    std::string distance_text = "—";
    // ITEM-012：最近掩码（仅 capture_latest_mask 时填充；plane 共享所有权，便于安全快照）
    bool mask_available = false;
    std::string mask_unavailable_reason = "NOT_CAPTURED";
    int mask_width = 0;
    int mask_height = 0;
    int mask_stride_bytes = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> mask_plane;
    long long mask_updates = 0;
    std::size_t tracks = 0;
    std::string envelope_summary;  // 最近 envelope JSON（boxes 为空时不含路径/凭据）
    std::map<std::string, long long> unavailable_by_reason;  // 不可用原因分布（可观察）
    std::string last_unavailable_reason;
    std::string last_unavailable_detail;
    std::vector<core::EnvelopeBox> boxes;
    core::EnvelopeStats envelope_stats;
    core::SemanticStats semantic_stats;
};

struct VisionWorkerSnapshot {
    bool worker_alive = false;
    bool running = false;
    std::string engine;
    long long steps = 0;
    long long infer_calls = 0;
    long long infer_errors = 0;
    long long events = 0;
    long long thermal_guard_rounds = 0;
    long long thermal_unknown_rounds = 0;
    long long submit_calls = 0;
    long long submit_dropped = 0;
    core::ThermalSample thermal;
    core::AiScheduleRound last_round;
    core::AiRuntime ai;                     // 供 core::ai_status_* 直接消费
    std::vector<VisionLaneSnapshot> lanes;
};

class VisionAiWorker {
public:
    VisionAiWorker(VisionWorkerConfig cfg, std::shared_ptr<VisionDetector> detector,
                   std::shared_ptr<ThermalSensor> thermal = nullptr,
                   std::shared_ptr<core::RuleEngine> rules = nullptr);
    ~VisionAiWorker();

    VisionAiWorker(const VisionAiWorker&) = delete;
    VisionAiWorker& operator=(const VisionAiWorker&) = delete;

    void start();
    // 非阻塞停止请求：立即置 running=false / worker_alive=false（快照即刻可见 AI 不可用）。
    void request_stop();
    // 请求 + join；最多等待当前一次推理完成（不持有调度锁）。
    void stop();
    bool running() const { return running_.load(); }

    // 取流层每帧调用：短锁/无锁入队（丢最旧），永不因 infer 阻塞。
    bool submit(const FrameLease& lease);
    void submit_frame(const AiFrame& frame);

    // 单轮：快照需求 -> 调度 -> 推理（锁外）-> 跟踪 -> 发布 -> 语义/规则。
    // 返回本轮真实推理路数。无工作线程时也可直接驱动（探针/测试）。
    int step();

    VisionWorkerSnapshot snapshot() const;
    AiFrameRouter& router() { return router_; }
    const AiFrameRouter& router() const { return router_; }

private:
    struct Lane;

    void loop();
    void fill_runtime(VisionWorkerSnapshot* snap) const;

    VisionWorkerConfig cfg_;
    std::shared_ptr<VisionDetector> detector_;
    std::shared_ptr<ThermalSensor> thermal_;
    std::shared_ptr<core::RuleEngine> rules_;
    AiFrameRouter router_;
    core::AiLaneScheduler scheduler_;

    std::vector<std::unique_ptr<Lane>> lanes_;
    std::atomic<bool> running_{false};
    std::atomic<bool> worker_alive_{false};
    std::thread thread_;

    mutable std::mutex mu_;
    // 受 mu_ 保护的遥测/状态（仅短临界区）。
    long long steps_ = 0;
    long long infer_calls_ = 0;
    long long infer_errors_ = 0;
    long long events_ = 0;
    long long thermal_guard_rounds_ = 0;
    long long thermal_unknown_rounds_ = 0;
    long long submit_calls_ = 0;
    long long submit_dropped_ = 0;
    core::ThermalSample thermal_sample;
    core::AiScheduleRound last_round_;
    long long started_mono_ms_ = 0;
};

}  // namespace media
}  // namespace railhmi
