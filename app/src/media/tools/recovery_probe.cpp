// railhmi-recovery (视频 ITEM-014)
// 异常恢复控制器（媒体侧）：
//   * 断流 -> 退避到期真实重建（stop(NULL/释放)->start）-> 真实新帧确认恢复（非 PLAYING）；
//   * 每次实际尝试只计一次，保留失败链计数；
//   * AUTH_FAILED/CREDENTIAL_MISSING 不自动重试；FORMAT/UNSUPPORTED 仅在能力/输入变化后重试；
//   * 单路重建不重启其他路；ONVIF 看门狗默认关闭（未配置显式 UNCONFIGURED）。
//
// 用法:
//   railhmi-recovery --config config --duration 8 --out rec.log --json rec.json
//   railhmi-recovery --selftest
//
// 退出码: 0 正常（含验收判定）；1 selftest 失败；2 参数错误；3 启动失败。

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/recovery_policy.h"
#include "railhmi/media/multi_stream.h"
#include "railhmi/media/recovery_controller.h"

namespace {

int run_selftest() {
    using namespace railhmi;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("selftest %s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    check(core::classify_source_error("gst", "Unauthorized 401") == core::SourceErrorKind::AuthFailed,
          "classify_auth");
    check(core::classify_source_error("gst", "credential_missing") == core::SourceErrorKind::CredentialMissing,
          "classify_credential");
    check(core::classify_source_error("gst", "no element") == core::SourceErrorKind::Unsupported,
          "classify_unsupported");
    check(core::classify_source_error("gst", "not-negotiated") == core::SourceErrorKind::Format,
          "classify_format");
    check(core::classify_source_error("gst", "connection refused") == core::SourceErrorKind::Network,
          "classify_network");

    core::RecoveryConfig cfg;
    cfg.base_backoff_ms = 100;
    core::RecoveryPolicy p(cfg);
    check(p.on_error(core::SourceErrorKind::Network, "cut", 0) == core::RecoveryAction::WaitBackoff,
          "policy_backoff");
    check(p.tick(100) == core::RecoveryAction::Rebuild && p.state().total_attempts == 1,
          "policy_rebuild_once");
    check(p.on_error(core::SourceErrorKind::AuthFailed, "401", 200) == core::RecoveryAction::StopAuth,
          "policy_auth_stop");
    core::RecoveryPolicy p2(cfg);
    check(p2.on_error(core::SourceErrorKind::Unsupported, "no element", 0) ==
              core::RecoveryAction::WaitCapabilityChange,
          "policy_capability_wait");
    p2.notify_capability_changed(10);
    check(p2.tick(10) == core::RecoveryAction::Rebuild, "policy_rebuild_after_change");
    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;
    std::string config_dir = "config";
    int duration_s = 8;
    std::string out, json;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--config") next(&config_dir);
        else if (a == "--duration") { std::string v; next(&v); duration_s = std::atoi(v.c_str()); }
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--selftest") return run_selftest();
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-recovery [--config dir] [--duration s] [--out log] [--json json] "
                        "[--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }

    std::ofstream fo(out, std::ios::trunc);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) { fo << line << "\n"; fo.flush(); }
    };

    const core::ConfigLoadResult cfg = core::load_config_dir(config_dir);
    int base_backoff = 500;
    if (cfg.ok && cfg.settings.thresholds.reconnect_backoff_ms > 0)
        base_backoff = cfg.settings.thresholds.reconnect_backoff_ms;
    emit("type=config reconnect_backoff_ms=" + std::to_string(base_backoff) +
         " duration_s=" + std::to_string(duration_s) + " channels=3");

    std::vector<media::ChannelSpec> specs;
    for (const char* id : {"CAM-01", "CAM-02", "CAM-03"}) {
        media::ChannelSpec sp;
        sp.camera_id = id;
        sp.cfg.camera_id = id;
        sp.cfg.source = media::SourceKind::Test;
        specs.push_back(sp);
    }
    media::MultiStreamManager mgr(specs);
    std::string err;
    const int started = mgr.start_all(&err);
    emit("type=start started=" + std::to_string(started) + " of=3");
    if (started < 3) { emit("type=acceptance result=FAIL reason=START"); return 0; }

    core::RecoveryConfig rc;
    rc.base_backoff_ms = base_backoff;
    rc.max_backoff_ms = std::max(base_backoff * 4, 4000);
    rc.max_attempts = 4;
    media::RecoveryControllerConfig ccfg;
    ccfg.policy = rc;

    media::RecoveryController c1(mgr.worker(0), ccfg);   // CAM-01 断流 -> 重建
    media::RecoveryController c2(mgr.worker(1), ccfg);   // CAM-02 鉴权失败 -> 不重试
    media::RecoveryController c3(mgr.worker(2), ccfg);   // CAM-03 不支持 -> 能力变化后重试
    media::RecoveryControllerConfig onvif_cfg;
    onvif_cfg.onvif_watchdog_enabled = true;             // 启用但未配置地址 -> UNCONFIGURED
    media::RecoveryController onvif_probe(mgr.worker(0), onvif_cfg);

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    auto now_ms = [&]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
    };

    // 预热到出帧。
    while (now_ms() < 2500) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (mgr.stats()[0].last_seq > 0 && mgr.stats()[1].last_seq > 0 && mgr.stats()[2].last_seq > 0) break;
    }
    const long long gen1_before = static_cast<long long>(mgr.stats()[0].generation);
    const long long gen2_before = static_cast<long long>(mgr.stats()[1].generation);
    const long long gen3_before = static_cast<long long>(mgr.stats()[2].generation);

    // 注入：CAM-01 断流；CAM-02 鉴权失败；CAM-03 不支持/格式（需能力变化）。
    mgr.request_cut(0);
    const long long t_cut = now_ms();
    c2.on_source_error("gst-resource-error-quark", "Unauthorized 401", now_ms());
    c3.on_source_error("gst-core-error-quark", "no element 'mppvideodec' (unsupported)", now_ms());

    long long t_rebuild1 = -1, t_rebuild2 = -1, t_rebuild3 = -1;
    int rebuild1_count = 0;
    bool c3_rebuilt_before_change = false;
    bool capability_changed = false;
    bool second_cut_done = false;
    bool recovered1_once = false;
    const long long change_at = 2500;
    while (now_ms() < duration_s * 1000) {
        const long long now = now_ms();
        const core::RecoveryAction a1 = c1.tick(now);
        const core::RecoveryAction a2 = c2.tick(now);
        const core::RecoveryAction a3 = c3.tick(now);
        if (a1 == core::RecoveryAction::Rebuild) {
            ++rebuild1_count;
            if (rebuild1_count == 1) t_rebuild1 = now;
            else if (rebuild1_count == 2) t_rebuild2 = now;
        }
        if (a3 == core::RecoveryAction::Rebuild) {
            if (!capability_changed) c3_rebuilt_before_change = true;
            if (t_rebuild3 < 0) t_rebuild3 = now;
        }
        (void)a2;
        // 第一次恢复后，再次注入**完全相同文本**的断流，验证恢复后去重失效、能再次重建。
        if (!recovered1_once) {
            const auto st1 = c1.status();
            if (st1.recovered && !st1.awaiting_recovery && st1.recovered_generation > gen1_before) {
                recovered1_once = true;
                if (!second_cut_done) { mgr.request_cut(0); second_cut_done = true; }
            }
        }
        if (!capability_changed && now >= change_at) {
            capability_changed = true;
            c3.notify_capability_changed(now);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    const auto s1 = c1.status();
    const auto s2 = c2.status();
    const auto s3 = c3.status();
    const std::vector<media::StreamStats> st = mgr.stats();
    const long long gen1_after = static_cast<long long>(st[0].generation);
    const long long gen2_after = static_cast<long long>(st[1].generation);
    const long long gen3_after = static_cast<long long>(st[2].generation);

    emit("type=recovery cam=CAM-01 " + s1.describe());
    emit("type=recovery cam=CAM-02 " + s2.describe());
    emit("type=recovery cam=CAM-03 " + s3.describe());
    emit("type=generations cam01=" + std::to_string(gen1_before) + "->" + std::to_string(gen1_after) +
         " cam02=" + std::to_string(gen2_before) + "->" + std::to_string(gen2_after) +
         " cam03=" + std::to_string(gen3_before) + "->" + std::to_string(gen3_after));
    emit("type=timing t_cut_ms=" + std::to_string(t_cut) + " rebuild1_ms=" + std::to_string(t_rebuild1) +
         " rebuild2_ms=" + std::to_string(t_rebuild2) + " rebuild3_ms=" + std::to_string(t_rebuild3) +
         " change_ms=" + std::to_string(change_at) + " second_cut=" +
         std::string(second_cut_done ? "true" : "false"));
    emit("type=home " + onvif_probe.status().describe());

    const bool rebuild_confirmed = s1.recovered && gen1_after > gen1_before &&
                                   s1.recovered_generation > gen1_before && st[0].last_seq > 0;
    const bool attempts_match_actual = s1.policy.total_attempts == rebuild1_count;
    const bool repeat_same_error_recovers = rebuild1_count == 2 && gen1_after == gen1_before + 2 &&
                                            s1.recovered_generation == gen1_before + 2;
    const bool backoff_respected = t_rebuild1 >= 0 && (t_rebuild1 - t_cut) >= base_backoff - 50;
    const bool auth_no_retry = s2.policy.stopped && s2.policy.total_attempts == 0 &&
                               gen2_after == gen2_before && st[1].last_seq > 0;
    const bool capability_gated = !c3_rebuilt_before_change && s3.policy.total_attempts == 1 &&
                                  t_rebuild3 >= change_at && s3.recovered && gen3_after > gen3_before;
    const bool isolation = gen2_after == gen2_before;   // CAM-01 重建未重启 CAM-02
    const bool onvif_off = onvif_probe.status().onvif_status == "unavailable:UNCONFIGURED" &&
                           !onvif_probe.onvif_tick(now_ms());
    const bool onvif_default = c1.status().onvif_status == "disabled";

    const bool ok = rebuild_confirmed && attempts_match_actual && repeat_same_error_recovers &&
                    backoff_respected && auth_no_retry && capability_gated && isolation &&
                    onvif_default && onvif_off;
    emit("type=acceptance rebuild_confirmed_by_new_frame=" +
         std::string(rebuild_confirmed ? "true" : "false") +
         " attempts_match_actual=" + std::string(attempts_match_actual ? "true" : "false") +
         " repeat_same_error_recovers=" + std::string(repeat_same_error_recovers ? "true" : "false") +
         " backoff_respected=" + std::string(backoff_respected ? "true" : "false") +
         " auth_no_retry=" + std::string(auth_no_retry ? "true" : "false") +
         " capability_gated=" + std::string(capability_gated ? "true" : "false") +
         " single_channel_isolation=" + std::string(isolation ? "true" : "false") +
         " onvif_default_off=" + std::string(onvif_default ? "true" : "false") +
         " onvif_unconfigured=" + std::string(onvif_off ? "true" : "false") +
         " result=" + std::string(ok ? "PASS" : "FAIL"));

    mgr.stop_all();

    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        j << "{\n  \"cam01\": {\"attempts\": " << s1.policy.total_attempts
          << ", \"rebuilds\": " << rebuild1_count
          << ", \"recovered_generation\": " << s1.recovered_generation
          << ", \"repeat_same_error_recovers\": " << (repeat_same_error_recovers ? "true" : "false")
          << ", \"recovered\": " << (s1.recovered ? "true" : "false") << "},\n";
        j << "  \"cam02\": {\"stopped\": " << (s2.policy.stopped ? "true" : "false")
          << ", \"attempts\": " << s2.policy.total_attempts << "},\n";
        j << "  \"cam03\": {\"attempts\": " << s3.policy.total_attempts
          << ", \"recovered_generation\": " << s3.recovered_generation << "},\n";
        j << "  \"acceptance\": " << (ok ? "true" : "false") << "\n}\n";
    }
    return 0;
}
