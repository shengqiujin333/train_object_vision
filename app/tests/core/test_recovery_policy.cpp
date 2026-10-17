// railhmi-core 异常恢复分类与重试策略测试（视频 ITEM-014 纯逻辑）。

#include "test_util.h"

#include "railhmi/core/recovery_policy.h"

namespace {

using namespace railhmi::core;

void test_classify() {
    railhmi_test::check(classify_source_error("gst", "Unauthorized 401") == SourceErrorKind::AuthFailed,
                        "classify auth 401");
    railhmi_test::check(classify_source_error("gst-resource-error-quark", "Authentication failed") ==
                            SourceErrorKind::AuthFailed,
                        "classify authentication");
    railhmi_test::check(classify_source_error("gst", "credential_missing env not set") ==
                            SourceErrorKind::CredentialMissing,
                        "classify credential");
    railhmi_test::check(classify_source_error("gst", "No such element or plugin 'foo'") ==
                            SourceErrorKind::Unsupported,
                        "classify unsupported");
    railhmi_test::check(classify_source_error("gst", "stream is not-negotiated") == SourceErrorKind::Format,
                        "classify format");
    railhmi_test::check(classify_source_error("gst", "connection refused") == SourceErrorKind::Network,
                        "classify network");
    railhmi_test::check(classify_source_error("gst", "simulated stream cut (FI-01)") ==
                            SourceErrorKind::Network,
                        "classify cut");
    railhmi_test::check(classify_source_error("gst", "read timed out") == SourceErrorKind::Timeout,
                        "classify timeout");
    railhmi_test::check(classify_source_error("", "") == SourceErrorKind::None, "classify empty");
    railhmi_test::check(classify_source_error("gst", "something odd") == SourceErrorKind::Other,
                        "classify other");
}

void test_backoff_and_attempts_counted_once() {
    RecoveryConfig cfg;
    cfg.base_backoff_ms = 1000;
    cfg.max_backoff_ms = 8000;
    RecoveryPolicy p(cfg);
    railhmi_test::check(p.on_error(SourceErrorKind::Network, "cut", 0) == RecoveryAction::WaitBackoff,
                        "net wait backoff");
    railhmi_test::check(p.state().failure_chain == 1 && p.state().current_backoff_ms == 1000,
                        "net chain/backoff");
    railhmi_test::check(p.tick(500) == RecoveryAction::None, "before due none");
    railhmi_test::check(p.tick(1000) == RecoveryAction::Rebuild, "due rebuild");
    railhmi_test::check(p.state().total_attempts == 1, "attempt counted once");
    railhmi_test::check(p.tick(1000) == RecoveryAction::None, "no double count same due");
    // 第二次失败 -> 退避翻倍。
    p.on_error(SourceErrorKind::Network, "cut again", 1500);
    railhmi_test::check(p.state().failure_chain == 2 && p.state().current_backoff_ms == 2000,
                        "second backoff doubled");
    railhmi_test::check(p.tick(3499) == RecoveryAction::None, "second not due");
    railhmi_test::check(p.tick(3500) == RecoveryAction::Rebuild, "second due");
    railhmi_test::check(p.state().total_attempts == 2, "attempts cumulative");
    railhmi_test::check(p.state().max_failure_chain == 2, "max chain retained");
}

void test_auth_no_retry() {
    RecoveryPolicy p;
    railhmi_test::check(p.on_error(SourceErrorKind::AuthFailed, "401", 0) == RecoveryAction::StopAuth,
                        "auth stop");
    railhmi_test::check(p.state().stopped && p.state().total_attempts == 0, "auth no attempt");
    railhmi_test::check(p.tick(1000000) == RecoveryAction::StopAuth, "auth never rebuilds");
    railhmi_test::check(p.state().total_attempts == 0, "auth still 0 attempts");
    RecoveryPolicy p2;
    railhmi_test::check(p2.on_error(SourceErrorKind::CredentialMissing, "cred", 0) == RecoveryAction::StopAuth,
                        "cred stop");
}

void test_capability_change_gate() {
    RecoveryPolicy p;
    railhmi_test::check(p.on_error(SourceErrorKind::Format, "not-negotiated", 0) ==
                            RecoveryAction::WaitCapabilityChange,
                        "format waits capability");
    railhmi_test::check(p.tick(100000) == RecoveryAction::WaitCapabilityChange, "format no rebuild");
    railhmi_test::check(p.state().total_attempts == 0, "format no attempt");
    p.notify_capability_changed(100000);
    railhmi_test::check(p.tick(100000) == RecoveryAction::Rebuild, "rebuild after capability change");
    railhmi_test::check(p.state().total_attempts == 1, "capability attempt counted");
}

void test_max_attempts_stop() {
    RecoveryConfig cfg;
    cfg.base_backoff_ms = 10;
    cfg.max_attempts = 2;
    RecoveryPolicy p(cfg);
    long long t = 0;
    RecoveryAction a = p.on_error(SourceErrorKind::Network, "x", t);
    a = p.tick(t + 10);           // attempt 1
    a = p.on_error(SourceErrorKind::Network, "x", t + 20);
    a = p.tick(t + 40);           // attempt 2
    a = p.on_error(SourceErrorKind::Network, "x", t + 60);  // chain 3 > max 2 -> stop
    railhmi_test::check(a == RecoveryAction::StopMaxAttempts && p.state().stopped, "max attempts stop");
    railhmi_test::check(p.state().total_attempts == 2, "stopped at max attempts");
}

void test_recovered_resets() {
    RecoveryPolicy p;
    p.on_error(SourceErrorKind::Network, "cut", 0);
    p.tick(1000);  // attempt
    p.on_recovered(2000);
    railhmi_test::check(p.state().failure_chain == 0 && !p.state().stopped &&
                            p.state().next_attempt_at_ms == 0,
                        "recovered resets chain/schedule");
    railhmi_test::check(p.state().total_attempts == 1, "attempts retained after recovery");
}

}  // namespace

void run_recovery_policy_tests() {
    test_classify();
    test_backoff_and_attempts_counted_once();
    test_auth_no_retry();
    test_capability_change_gate();
    test_max_attempts_stop();
    test_recovered_resets();
}
