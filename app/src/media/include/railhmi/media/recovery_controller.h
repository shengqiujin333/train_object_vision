#pragma once

// railhmi-media: 异常恢复控制器（视频 ITEM-014，target 专用）。
//
// 在 CameraWorker 之上：
//   * 从 worker 的 last_error（或显式上报）分类错误，按 core::RecoveryPolicy 决策；
//   * 退避到期执行**真实会话重建**（CameraWorker::request_restart → stop(NULL/释放)→start 重建）；
//   * 恢复必须由**真实新帧**确认（generation 前进且 last_seq>0 且 presentable），不采信 PLAYING；
//   * AUTH_FAILED/CREDENTIAL_MISSING 不自动重试；FORMAT/UNSUPPORTED 仅在能力/输入变化后重试；
//   * ONVIF 看门狗为受控可选能力，默认关闭；未配置时明确 unavailable/UNCONFIGURED。
// 每次实际重建尝试只计一次，并保留失败链计数。

#include <cstdint>
#include <string>

#include "railhmi/core/recovery_policy.h"

namespace railhmi {
namespace media {

class CameraWorker;

struct RecoveryControllerConfig {
    core::RecoveryConfig policy;
    bool onvif_watchdog_enabled = false;   // 默认关闭
    std::string onvif_address;             // 未配置 -> unavailable/UNCONFIGURED
};

struct RecoveryControllerStatus {
    core::RecoveryState policy;
    bool awaiting_recovery = false;        // 已请求重建，等待真实新帧确认
    bool recovered = false;                // 最近一次重建由真实新帧确认
    long long recovered_generation = 0;
    long long recovery_latency_ms = -1;    // 请求重建 -> 真实新帧
    std::string onvif_status;              // disabled / unavailable:UNCONFIGURED / unsupported:no_protocol
    std::string describe() const;
};

class RecoveryController {
public:
    RecoveryController(CameraWorker* worker, RecoveryControllerConfig cfg = RecoveryControllerConfig());

    // 显式上报一次源错误（分类 -> 策略）。
    core::RecoveryAction on_source_error(const std::string& domain, const std::string& message,
                                         long long now_ms);
    // 周期推进：轮询 worker 的 last_error 新值、确认恢复、退避到期触发真实重建。
    core::RecoveryAction tick(long long now_ms);
    void notify_capability_changed(long long now_ms);

    // ONVIF 看门狗（受控可选，默认关闭）；返回是否执行了动作。
    bool onvif_tick(long long now_ms);

    RecoveryControllerStatus status() const;
    const core::RecoveryPolicy& policy() const { return policy_; }

private:
    CameraWorker* worker_;
    RecoveryControllerConfig cfg_;
    core::RecoveryPolicy policy_;
    std::string last_seen_error_;
    long long pending_gen_ = -1;
    long long rebuild_at_ms_ = -1;
    bool awaiting_recovery_ = false;
    bool recovered_ = false;
    long long recovered_gen_ = 0;
    long long recovery_latency_ms_ = -1;
};

}  // namespace media
}  // namespace railhmi
