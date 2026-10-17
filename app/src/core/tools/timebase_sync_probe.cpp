// railhmi-timebase-sync (视频 ITEM-015)
// 时间基准与回放同步（core 纯逻辑 + 真实时钟）：
//   * TimeBase.calibrate(wall,mono) + 单调↔UTC 换算（单次调用、往返一致）；
//   * 未校准不得写 UTC 业务事件（try_to_wall_ms/can_write_utc 显式拒绝）；
//   * 回放按各路实际播放头（UTC）求 max−min，超 replay_sync_threshold_ms 显式提示；
//     缺任一路有效时钟 -> available=false；API 不接收固定 offsets；
//   * 用户 seek 递增 request generation，陈旧回调丢弃；定位未到达不提前显示已定位。
//
// 说明：实际视频播放头由 media provider（ITEM-013）以 actual decoded position 提供；
// 本探针用**真实读取的单调时钟**构造各路播放头输入，验证统一 UTC 时间轴与同步/seek 模型。
//
// 用法:
//   railhmi-timebase-sync --config config --out sync.log --json sync.json
//   railhmi-timebase-sync --selftest
//
// 退出码: 0 正常（含验收判定）；1 selftest 失败；2 参数错误。

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/replay_sync.h"
#include "railhmi/core/timebase.h"

namespace {

int run_selftest() {
    using namespace railhmi;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("selftest %s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    core::TimeBase tb;
    long long w = -1;
    check(!tb.can_write_utc() && !tb.try_to_wall_ms(1, &w), "uncalibrated_guard");
    tb.calibrate(1'700'000'000'000LL, 5'000'000LL);
    check(tb.try_to_wall_ms(6'000'000LL, &w) && w == 1'700'000'001'000LL, "calibrate_convert");
    check(tb.to_mono_us(1'700'000'001'000LL) == 6'000'000LL, "roundtrip");

    std::map<std::string, long long> close{{"CAM-01", 1000}, {"CAM-02", 1100}};
    check(core::compute_replay_sync({"CAM-01", "CAM-02"}, close, true, 300).in_sync, "sync_in");
    std::map<std::string, long long> spread{{"CAM-01", 1000}, {"CAM-02", 1400}};
    auto out = core::compute_replay_sync({"CAM-01", "CAM-02"}, spread, true, 300);
    check(!out.in_sync && !out.prompt.empty(), "sync_prompt");
    check(!core::compute_replay_sync({"CAM-01", "CAM-02"}, close, false, 300).available, "sync_uncalibrated");
    std::map<std::string, long long> one{{ "CAM-01", 1000 }};
    auto miss = core::compute_replay_sync({ "CAM-01", "CAM-02" }, one, true, 300);
    check(!miss.available && miss.reason == "MISSING_CLOCK", "sync_missing_clock");
    core::SeekController sc(100);
    const long long g = sc.request_seek(5000);
    check(!sc.on_position(g - 1, 5000), "stale_discarded");
    check(sc.on_position(g, 4900) && sc.state().reached, "reached");
    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;
    std::string config_dir = "config";
    std::string out, json;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--config") next(&config_dir);
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--selftest") return run_selftest();
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-timebase-sync [--config dir] [--out log] [--json json] [--selftest]\n");
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
    int threshold = 300;
    if (cfg.ok && cfg.settings.thresholds.replay_sync_threshold_ms > 0)
        threshold = cfg.settings.thresholds.replay_sync_threshold_ms;
    emit("type=config replay_sync_threshold_ms=" + std::to_string(threshold));

    // 1) 启动校准 + 换算（单次调用、往返一致）。
    core::TimeBase tb;
    const long long wall = core::TimeBase::system_now_ms();
    const long long mono = core::TimeBase::steady_now_us();
    tb.calibrate(wall, mono);
    long long w1 = -1, w2 = -1;
    const bool conv1 = tb.try_to_wall_ms(mono + 1'000'000, &w1);
    const bool conv2 = tb.try_to_wall_ms(mono + 2'000'000, &w2);
    const long long back = tb.to_mono_us(w1);
    const bool roundtrip = (back == mono + 1'000'000) && (w2 - w1 == 1000);
    emit("type=timebase calibrated=" + std::string(tb.calibrated() ? "true" : "false") +
         " can_write_utc=" + std::string(tb.can_write_utc() ? "true" : "false") +
         " wall_ms=" + std::to_string(wall) + " mono_us=" + std::to_string(mono) +
         " +1s_wall=" + std::to_string(w1) + " roundtrip=" + std::string(roundtrip ? "true" : "false"));

    // 2) 未校准守卫：不得写 UTC 业务事件。
    core::TimeBase uncal;
    long long uw = 12345;
    const bool uncal_reject = !uncal.can_write_utc() && !uncal.try_to_wall_ms(mono, &uw) && uw == 12345;
    emit("type=uncalibrated can_write_utc=" + std::string(uncal.can_write_utc() ? "true" : "false") +
         " try_to_wall_ms=false utc_event_written=false reject=" +
         std::string(uncal_reject ? "true" : "false"));

    // 3) 三路实际播放头（各读一次真实单调时钟，经校准转 UTC）。
    std::vector<std::string> cams = {"CAM-01", "CAM-02", "CAM-03"};
    std::map<std::string, long long> heads;
    for (std::size_t i = 0; i < cams.size(); ++i) {
        long long h = 0;
        tb.try_to_wall_ms(core::TimeBase::steady_now_us(), &h);
        heads[cams[i]] = h;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    auto in_sync = core::compute_replay_sync(cams, heads, tb.calibrated(), threshold);
    emit("type=sync case=real_heads source=calibrated_monotonic_clock status=" +
         std::string(in_sync.available ? "available" : "unavailable") +
         " in_sync=" + std::string(in_sync.in_sync ? "true" : "false") +
         " max_deviation_ms=" + std::to_string(in_sync.max_deviation_ms) +
         " prompt=" + in_sync.prompt);

    // 4) 注入三路时间差（>阈值）——真实播放头集合，非固定 offsets。
    std::map<std::string, long long> skewed = heads;
    skewed["CAM-02"] = heads["CAM-01"] + 400;
    skewed["CAM-03"] = heads["CAM-01"] + 800;
    auto out_sync = core::compute_replay_sync(cams, skewed, tb.calibrated(), threshold);
    emit("type=sync case=skewed in_sync=" + std::string(out_sync.in_sync ? "true" : "false") +
         " max_deviation_ms=" + std::to_string(out_sync.max_deviation_ms) +
         " prompt=" + out_sync.prompt);

    // 5) 缺一路有效时钟 -> available=false。
    std::map<std::string, long long> partial = heads;
    partial.erase("CAM-03");
    auto missing = core::compute_replay_sync(cams, partial, tb.calibrated(), threshold);
    emit("type=sync case=missing_clock available=" + std::string(missing.available ? "true" : "false") +
         " reason=" + missing.reason + " missing=" +
         (missing.missing.empty() ? std::string("-") : missing.missing[0]));

    // 6) seek 世代：陈旧回调丢弃；定位未到达不提前显示已定位。
    core::SeekController sc(1000);
    const long long target = heads["CAM-01"] + 30000;
    const long long g1 = sc.request_seek(target);
    const bool stale0 = sc.on_position(g1 - 1, target);            // 陈旧 -> false
    const bool accepted = sc.on_position(g1, target - 5000);       // 匹配但未达 -> locating
    const bool premature = sc.state().reached;                     // 必须 false
    const long long g2 = sc.request_seek(target + 1000);
    const bool stale1 = sc.on_position(g1, target);                // 旧世代 -> false
    const bool pos_unchanged = sc.state().position_ms == target - 5000;
    const bool reached = sc.on_position(g2, target + 1000);        // 达到目标容差
    emit("type=seek gen1=" + std::to_string(g1) + " gen2=" + std::to_string(g2) +
         " stale0_discarded=" + std::string(!stale0 ? "true" : "false") +
         " accepted=" + std::string(accepted ? "true" : "false") +
         " premature_located=" + std::string(premature ? "true" : "false") +
         " stale1_discarded=" + std::string(!stale1 ? "true" : "false") +
         " stale_pos_unchanged=" + std::string(pos_unchanged ? "true" : "false") +
         " reached=" + std::string(reached && sc.state().reached ? "true" : "false"));

    const bool ok = tb.calibrated() && tb.can_write_utc() && conv1 && conv2 && roundtrip && uncal_reject &&
                    in_sync.available && in_sync.in_sync && !out_sync.in_sync && !out_sync.prompt.empty() &&
                    out_sync.max_deviation_ms > threshold && !missing.available &&
                    missing.reason == "MISSING_CLOCK" && !stale0 && accepted && !premature && !stale1 &&
                    pos_unchanged && reached && sc.state().reached && threshold == 300;
    emit("type=acceptance timebase_calibrated=" + std::string(tb.calibrated() ? "true" : "false") +
         " conversion_roundtrip=" + std::string(roundtrip ? "true" : "false") +
         " uncalibrated_no_utc=" + std::string(uncal_reject ? "true" : "false") +
         " sync_real_in_sync=" + std::string(in_sync.in_sync ? "true" : "false") +
         " sync_prompt_over_threshold=" + std::string(!out_sync.in_sync && !out_sync.prompt.empty() ? "true" : "false") +
         " sync_missing_clock_unavailable=" + std::string(!missing.available ? "true" : "false") +
         " seek_stale_discarded=" + std::string((!stale0 && !stale1 && pos_unchanged) ? "true" : "false") +
         " seek_not_premature=" + std::string(!premature ? "true" : "false") +
         " seek_reached=" + std::string((reached && sc.state().reached) ? "true" : "false") +
         " threshold_from_config=" + std::string(threshold == 300 ? "true" : "false") +
         " result=" + std::string(ok ? "PASS" : "FAIL"));

    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        j << "{\n  \"threshold_ms\": " << threshold << ",\n";
        j << "  \"sync_real\": {\"in_sync\": " << (in_sync.in_sync ? "true" : "false")
          << ", \"max_deviation_ms\": " << in_sync.max_deviation_ms << "},\n";
        j << "  \"sync_skewed\": {\"in_sync\": " << (out_sync.in_sync ? "true" : "false")
          << ", \"max_deviation_ms\": " << out_sync.max_deviation_ms << "},\n";
        j << "  \"missing_clock\": \"" << missing.reason << "\",\n";
        j << "  \"seek\": {\"gen1\": " << g1 << ", \"gen2\": " << g2
          << ", \"stale_discarded\": " << (!stale0 && !stale1 ? "true" : "false")
          << ", \"reached\": " << (sc.state().reached ? "true" : "false") << "},\n";
        j << "  \"acceptance\": " << (ok ? "true" : "false") << "\n}\n";
    }
    return 0;
}
