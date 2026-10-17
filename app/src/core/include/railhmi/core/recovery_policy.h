#pragma once

// railhmi-core: 异常恢复分类与重试策略（视频 ITEM-014 / 接口契约 §6/§8）。
//
// 把上游错误分类（AUTH_FAILED/CREDENTIAL_MISSING/UNSUPPORTED/FORMAT/NETWORK/TIMEOUT/OTHER），
// 据此决定：按退避重试 / 停止（鉴权失败不无限重试）/ 等待输入或能力变化（格式/不支持）。
// 每次**实际重建尝试只计一次**（total_attempts），并保留失败链计数（failure_chain/max_failure_chain）。
// 恢复由真实新帧确认（调用方在确认真帧后调 on_recovered），不以 PLAYING 返回值判定。
// 纯逻辑 + 显式单调时钟；无 GStreamer/线程依赖。

#include <cstdint>
#include <string>

namespace railhmi {
namespace core {

enum class SourceErrorKind {
    None,
    AuthFailed,
    CredentialMissing,
    Unsupported,
    Format,
    Network,
    Timeout,
    Other,
};
const char* to_string(SourceErrorKind k);

// 从错误域/消息文本分类（大小写不敏感的关键字判定）。
SourceErrorKind classify_source_error(const std::string& domain, const std::string& message);

enum class RecoveryAction {
    None,                   // 无动作
    WaitBackoff,            // 已排程退避
    Rebuild,                // 退避到期，执行真实重建
    StopAuth,               // 鉴权/凭据失败：不自动重试
    WaitCapabilityChange,   // 格式/不支持：等待输入或模型能力变化
    StopMaxAttempts,        // 超过连续失败上限：停止
};
const char* to_string(RecoveryAction a);

struct RecoveryConfig {
    int base_backoff_ms = 1000;    // 首次退避
    int max_backoff_ms = 30000;    // 指数退避上限
    int max_attempts = 5;          // 连续失败上限（超出 -> StopMaxAttempts）
    int healthy_reset_ms = 5000;   // 保留字段：持续出帧时长（确认由调用方按真帧调用 on_recovered）
};

struct RecoveryState {
    SourceErrorKind last_kind = SourceErrorKind::None;
    std::string last_error;
    int failure_chain = 0;         // 当前连续失败计数
    int max_failure_chain = 0;     // 保留的失败链峰值
    int total_attempts = 0;        // 实际重建尝试次数（每次一个）
    int current_backoff_ms = 0;    // 本次退避
    long long next_attempt_at_ms = 0;
    bool stopped = false;
    bool capability_wait = false;
    std::string stop_reason;
};

class RecoveryPolicy {
public:
    explicit RecoveryPolicy(RecoveryConfig cfg = RecoveryConfig());

    // 记录一次源错误，返回下一步动作并更新状态。
    RecoveryAction on_error(SourceErrorKind kind, const std::string& message, long long now_ms);
    // 周期推进：退避到期则登记一次实际尝试并返回 Rebuild；否则返回当前等待动作/None。
    RecoveryAction tick(long long now_ms);
    // 真实新帧确认恢复：清除失败链/退避/停止与能力等待。
    void on_recovered(long long now_ms);
    // 输入或模型能力变化：允许 UNSUPPORTED/FORMAT 立刻重试。
    void notify_capability_changed(long long now_ms);

    void reset();

    const RecoveryState& state() const { return st_; }
    RecoveryConfig config() const { return cfg_; }
    std::string describe() const;

private:
    RecoveryConfig cfg_;
    RecoveryState st_;
};

// 描述一份恢复状态（供控制器/诊断复用）。
std::string describe_recovery_state(const RecoveryState& st);

}  // namespace core
}  // namespace railhmi
