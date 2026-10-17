#include "railhmi/media/recovery_controller.h"

#include "railhmi/media/camera_worker.h"

namespace railhmi {
namespace media {

RecoveryController::RecoveryController(CameraWorker* worker, RecoveryControllerConfig cfg)
    : worker_(worker), cfg_(std::move(cfg)), policy_(cfg_.policy) {}

core::RecoveryAction RecoveryController::on_source_error(const std::string& domain,
                                                         const std::string& message, long long now_ms) {
    last_seen_error_ = message;
    const core::SourceErrorKind kind = core::classify_source_error(domain, message);
    return policy_.on_error(kind, message, now_ms);
}

core::RecoveryAction RecoveryController::tick(long long now_ms) {
    if (!worker_) return core::RecoveryAction::None;
    const StreamStats s = worker_->stats();

    // 代际变化（真实重建）使旧错误去重失效：新会话里同一文本错误必须重新分类。
    const long long gen = static_cast<long long>(s.generation);
    if (gen != last_gen_) {
        last_gen_ = gen;
        last_seen_error_.clear();
    }
    // 轮询 worker 的脱敏 last_error：空 -> 去重失效；新值 -> 分类一次。
    if (s.last_error.empty()) {
        last_seen_error_.clear();
    } else if (s.last_error != last_seen_error_) {
        last_seen_error_ = s.last_error;
        const core::SourceErrorKind kind = core::classify_source_error("gst", s.last_error);
        if (kind != core::SourceErrorKind::None) policy_.on_error(kind, s.last_error, now_ms);
    }

    // 恢复确认：必须由真实新帧（generation 前进 + 有帧 + presentable）确认，非 PLAYING 返回值。
    if (awaiting_recovery_) {
        if (static_cast<long long>(s.generation) > pending_gen_ && s.last_seq > 0 && s.presentable) {
            policy_.on_recovered(now_ms);
            recovered_ = true;
            recovered_gen_ = static_cast<long long>(s.generation);
            recovery_latency_ms_ = rebuild_at_ms_ >= 0 ? (now_ms - rebuild_at_ms_) : -1;
            awaiting_recovery_ = false;
            pending_gen_ = -1;
            last_seen_error_.clear();   // 恢复后同一文本错误视为新错误
            last_gen_ = static_cast<long long>(s.generation);
        }
        return core::RecoveryAction::None;
    }

    const core::RecoveryAction act = policy_.tick(now_ms);
    if (act == core::RecoveryAction::Rebuild) {
        pending_gen_ = static_cast<long long>(s.generation);
        rebuild_at_ms_ = now_ms;
        awaiting_recovery_ = true;
        worker_->request_restart();   // 真实重建：stop(NULL/释放) → start(重建)
    }
    return act;
}

void RecoveryController::notify_capability_changed(long long now_ms) {
    policy_.notify_capability_changed(now_ms);
}

bool RecoveryController::onvif_tick(long long /*now_ms*/) {
    if (!cfg_.onvif_watchdog_enabled) return false;      // 默认关闭
    if (cfg_.onvif_address.empty()) return false;        // 未配置：不动作
    return false;                                        // 受控可选：协议未登记 -> 不动作（不猜通道）
}

std::string RecoveryControllerStatus::describe() const {
    std::string o = core::describe_recovery_state(policy);
    o += " awaiting_recovery=" + std::string(awaiting_recovery ? "true" : "false");
    o += " recovered=" + std::string(recovered ? "true" : "false");
    o += " recovered_generation=" + std::to_string(recovered_generation);
    o += " recovery_latency_ms=" + std::to_string(recovery_latency_ms);
    o += " onvif=" + onvif_status;
    return o;
}

RecoveryControllerStatus RecoveryController::status() const {
    RecoveryControllerStatus out;
    out.policy = policy_.state();
    out.awaiting_recovery = awaiting_recovery_;
    out.recovered = recovered_;
    out.recovered_generation = recovered_gen_;
    out.recovery_latency_ms = recovery_latency_ms_;
    if (!cfg_.onvif_watchdog_enabled) {
        out.onvif_status = "disabled";
    } else if (cfg_.onvif_address.empty()) {
        out.onvif_status = "unavailable:UNCONFIGURED";
    } else {
        out.onvif_status = "unsupported:no_protocol";
    }
    return out;
}

}  // namespace media
}  // namespace railhmi
