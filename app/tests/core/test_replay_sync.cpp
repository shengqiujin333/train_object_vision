// railhmi-core 时间基准守卫、回放同步与 seek 世代测试（视频 ITEM-015 纯逻辑）。

#include "test_util.h"

#include <map>
#include <string>
#include <vector>

#include "railhmi/core/replay_sync.h"
#include "railhmi/core/timebase.h"

namespace {

using namespace railhmi::core;

void test_timebase_calibrate_and_guard() {
    TimeBase tb;
    railhmi_test::check(!tb.calibrated() && !tb.can_write_utc(), "uncalibrated guard");
    long long w = -1;
    railhmi_test::check(!tb.try_to_wall_ms(123456, &w) && w == -1, "try_to_wall_ms false when uncalibrated");

    tb.calibrate(1'700'000'000'000LL, 5'000'000LL);   // wall 1.7e12 ms, mono 5e6 us
    railhmi_test::check(tb.calibrated() && tb.can_write_utc(), "calibrated");
    // mono + 1s -> wall + 1000ms
    railhmi_test::check(tb.try_to_wall_ms(6'000'000LL, &w) && w == 1'700'000'001'000LL, "mono->wall");
    // 往返一致
    const long long mono = tb.to_mono_us(1'700'000'001'000LL);
    railhmi_test::check(mono == 6'000'000LL, "wall->mono roundtrip");
}

void test_sync_uncalibrated_and_missing() {
    std::map<std::string, long long> heads{{"CAM-01", 1000}, {"CAM-02", 1050}};
    auto uncal = compute_replay_sync({"CAM-01", "CAM-02"}, heads, false, 300);
    railhmi_test::check(!uncal.available && uncal.reason == "UNCALIBRATED", "sync uncalibrated");

    auto missing = compute_replay_sync({"CAM-01", "CAM-02", "CAM-03"}, heads, true, 300);
    railhmi_test::check(!missing.available && missing.reason == "MISSING_CLOCK" &&
                            missing.missing.size() == 1 && missing.missing[0] == "CAM-03",
                        "sync missing clock");
}

void test_sync_in_and_out() {
    std::map<std::string, long long> close{{"CAM-01", 1000}, {"CAM-02", 1200}, {"CAM-03", 1150}};
    auto in = compute_replay_sync({"CAM-01", "CAM-02", "CAM-03"}, close, true, 300);
    railhmi_test::check(in.available && in.in_sync && in.max_deviation_ms == 200 && in.prompt.empty(),
                        "sync in_sync <=300");

    std::map<std::string, long long> spread{{"CAM-01", 1000}, {"CAM-02", 1400}, {"CAM-03", 1800}};
    auto out = compute_replay_sync({"CAM-01", "CAM-02", "CAM-03"}, spread, true, 300);
    railhmi_test::check(out.available && !out.in_sync && out.max_deviation_ms == 800 && !out.prompt.empty(),
                        "sync out prompt");

    auto single = compute_replay_sync({"CAM-01"}, {{"CAM-01", 1000}}, true, 300);
    railhmi_test::check(single.available && single.in_sync, "sync single available");

    // 阈值可配：同一组在 1000ms 阈值下判定为同步。
    auto wide = compute_replay_sync({"CAM-01", "CAM-02", "CAM-03"}, spread, true, 1000);
    railhmi_test::check(wide.in_sync && wide.prompt.empty(), "configurable threshold");
}

void test_seek_generation() {
    SeekController sc(100);
    const long long g1 = sc.request_seek(5000);
    railhmi_test::check(g1 == 1 && sc.state().locating && !sc.state().reached, "seek1 locating");
    // 陈旧回调（世代 0/1 之外）被丢弃。
    railhmi_test::check(!sc.on_position(0, 5000), "stale gen0 discarded");
    railhmi_test::check(sc.state().position_ms == 0, "stale no position update");
    // 匹配世代但未到达目标 -> 仍 locating，不提前显示已定位。
    railhmi_test::check(sc.on_position(g1, 2000), "gen1 accepted");
    railhmi_test::check(!sc.state().reached && sc.state().locating && sc.state().position_ms == 2000,
                        "not reached yet");
    // 新 seek 递增世代，旧世代回调变陈旧。
    const long long g2 = sc.request_seek(9000);
    railhmi_test::check(g2 == 2, "gen increments");
    railhmi_test::check(!sc.on_position(g1, 5000), "gen1 now stale");
    railhmi_test::check(!sc.state().reached && sc.state().position_ms == 2000, "stale does not update");
    // 达到目标容差 -> reached。
    railhmi_test::check(sc.on_position(g2, 9050), "gen2 accepted");
    railhmi_test::check(sc.state().reached && !sc.state().locating && sc.state().position_ms == 9050,
                        "reached within tolerance");
    sc.reset();
    railhmi_test::check(sc.state().generation == 0 && !sc.state().reached, "reset");
}

}  // namespace

void run_replay_sync_tests() {
    test_timebase_calibrate_and_guard();
    test_sync_uncalibrated_and_missing();
    test_sync_in_and_out();
    test_seek_generation();
}
