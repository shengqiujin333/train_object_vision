#include "railhmi/core/recovery_policy.h"

#include <algorithm>
#include <cctype>

namespace railhmi {
namespace core {

namespace {

std::string lower(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

bool has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

}  // namespace

const char* to_string(SourceErrorKind k) {
    switch (k) {
        case SourceErrorKind::None: return "NONE";
        case SourceErrorKind::AuthFailed: return "AUTH_FAILED";
        case SourceErrorKind::CredentialMissing: return "CREDENTIAL_MISSING";
        case SourceErrorKind::Unsupported: return "UNSUPPORTED";
        case SourceErrorKind::Format: return "FORMAT";
        case SourceErrorKind::Network: return "NETWORK";
        case SourceErrorKind::Timeout: return "TIMEOUT";
        case SourceErrorKind::Other: return "OTHER";
    }
    return "OTHER";
}

SourceErrorKind classify_source_error(const std::string& domain, const std::string& message) {
    const std::string t = lower(domain + " " + message);
    if (t.empty() || t == " ") return SourceErrorKind::None;
    // 鉴权/凭据优先（不可重试）。
    if (has(t, "401") || has(t, "unauthorized") || has(t, "authentication") ||
        has(t, "auth failed") || has(t, "auth_failed") || has(t, "forbidden") || has(t, "403")) {
        return SourceErrorKind::AuthFailed;
    }
    if (has(t, "credential") || has(t, "no password") || has(t, "missing password") ||
        has(t, "password required")) {
        return SourceErrorKind::CredentialMissing;
    }
    // 能力/格式问题（需能力或输入变化后再试）。
    if (has(t, "unsupported") || has(t, "no element") || has(t, "no such element") ||
        has(t, "not implemented")) {
        return SourceErrorKind::Unsupported;
    }
    if (has(t, "not-negotiated") || has(t, "not negotiated") || has(t, "format") ||
        has(t, "caps") || has(t, "could not decode") || has(t, "decoder")) {
        return SourceErrorKind::Format;
    }
    if (has(t, "timeout") || has(t, "timed out") || has(t, "deadline")) return SourceErrorKind::Timeout;
    if (has(t, "connection") || has(t, "network") || has(t, "unreachable") || has(t, "refused") ||
        has(t, "eos") || has(t, "cut") || has(t, "reset") || has(t, "broken pipe") || has(t, "source")) {
        return SourceErrorKind::Network;
    }
    return SourceErrorKind::Other;
}

const char* to_string(RecoveryAction a) {
    switch (a) {
        case RecoveryAction::None: return "none";
        case RecoveryAction::WaitBackoff: return "wait_backoff";
        case RecoveryAction::Rebuild: return "rebuild";
        case RecoveryAction::StopAuth: return "stop_auth";
        case RecoveryAction::WaitCapabilityChange: return "wait_capability_change";
        case RecoveryAction::StopMaxAttempts: return "stop_max_attempts";
    }
    return "none";
}

RecoveryPolicy::RecoveryPolicy(RecoveryConfig cfg) : cfg_(cfg) {}

RecoveryAction RecoveryPolicy::on_error(SourceErrorKind kind, const std::string& message,
                                        long long now_ms) {
    st_.last_kind = kind;
    st_.last_error = message;
    switch (kind) {
        case SourceErrorKind::AuthFailed:
        case SourceErrorKind::CredentialMissing:
            // 不自动反复重试；保留失败链（不增加 attempts）。
            st_.stopped = true;
            st_.stop_reason = to_string(kind);
            st_.next_attempt_at_ms = 0;
            return RecoveryAction::StopAuth;
        case SourceErrorKind::Unsupported:
        case SourceErrorKind::Format:
            // 仅在输入或模型能力变化后重试。
            st_.capability_wait = true;
            st_.next_attempt_at_ms = 0;
            return RecoveryAction::WaitCapabilityChange;
        case SourceErrorKind::None:
            return RecoveryAction::None;
        default:
            break;
    }
    // 网络/超时/其他：指数退避重试。
    st_.failure_chain += 1;
    st_.max_failure_chain = std::max(st_.max_failure_chain, st_.failure_chain);
    if (st_.failure_chain > cfg_.max_attempts) {
        st_.stopped = true;
        st_.stop_reason = "MAX_ATTEMPTS";
        st_.next_attempt_at_ms = 0;
        return RecoveryAction::StopMaxAttempts;
    }
    int backoff = cfg_.base_backoff_ms;
    for (int i = 1; i < st_.failure_chain; ++i) backoff *= 2;
    backoff = std::min(backoff, cfg_.max_backoff_ms);
    st_.current_backoff_ms = backoff;
    st_.next_attempt_at_ms = now_ms + backoff;
    return RecoveryAction::WaitBackoff;
}

RecoveryAction RecoveryPolicy::tick(long long now_ms) {
    if (st_.stopped) {
        return st_.stop_reason == "MAX_ATTEMPTS" ? RecoveryAction::StopMaxAttempts
                                                   : RecoveryAction::StopAuth;
    }
    if (st_.capability_wait) return RecoveryAction::WaitCapabilityChange;
    if (st_.next_attempt_at_ms > 0 && now_ms >= st_.next_attempt_at_ms) {
        st_.total_attempts += 1;       // 每次实际尝试只计一次
        st_.next_attempt_at_ms = 0;    // 由下一次 on_error 重新排程
        return RecoveryAction::Rebuild;
    }
    return RecoveryAction::None;
}

void RecoveryPolicy::on_recovered(long long /*now_ms*/) {
    st_.failure_chain = 0;
    st_.current_backoff_ms = 0;
    st_.next_attempt_at_ms = 0;
    st_.stopped = false;
    st_.capability_wait = false;
    st_.stop_reason.clear();
}

void RecoveryPolicy::notify_capability_changed(long long now_ms) {
    if (!st_.capability_wait) return;
    st_.capability_wait = false;
    st_.stopped = false;
    st_.stop_reason.clear();
    st_.current_backoff_ms = 0;
    st_.next_attempt_at_ms = now_ms;   // 立刻允许一次重试
}

void RecoveryPolicy::reset() { st_ = RecoveryState{}; }

std::string describe_recovery_state(const RecoveryState& st) {
    std::string o = "kind=";
    o += to_string(st.last_kind);
    o += " failure_chain=" + std::to_string(st.failure_chain);
    o += " max_failure_chain=" + std::to_string(st.max_failure_chain);
    o += " attempts=" + std::to_string(st.total_attempts);
    o += " backoff_ms=" + std::to_string(st.current_backoff_ms);
    o += " stopped=" + std::string(st.stopped ? "true" : "false");
    o += " capability_wait=" + std::string(st.capability_wait ? "true" : "false");
    if (!st.stop_reason.empty()) o += " stop_reason=" + st.stop_reason;
    return o;
}

std::string RecoveryPolicy::describe() const { return describe_recovery_state(st_); }

}  // namespace core
}  // namespace railhmi
