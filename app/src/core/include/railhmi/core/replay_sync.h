#pragma once

// railhmi-core: 统一 UTC 回放时间轴、同步偏差与 seek 世代（视频 ITEM-015）。
//
// 回放同步按**各路实际播放头**（UTC 毫秒）求 max−min：超过 replay_sync_threshold_ms
// （默认 300，来自 thresholds.yaml）显式提示同步偏差；缺任一路有效时钟或时间基准未校准
// 则 available=false（不显示恒真“在同步”）。API 只消费实际播放头，不接受固定 offsets。
// 用户 seek 递增 request generation，陈旧回调被丢弃；定位未到达（实际播放头未达目标容差）
// 不得提前显示已定位。
// 纯逻辑 + stdlib；实际播放头由 media provider 提供。

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "railhmi/core/replay.h"

namespace railhmi {
namespace core {

struct ReplaySyncStatus {
    bool available = false;         // 所有锁定路均有有效时钟且时间基准已校准
    bool in_sync = false;
    long long max_deviation_ms = 0;
    std::string prompt;             // 超阈值时的显式提示（非空）
    std::string reason;             // "UNCALIBRATED" / "MISSING_CLOCK" / ""
    std::vector<std::string> missing;   // 缺有效播放头的相机
};

// 以锁定各路**实际播放头**（UTC 毫秒）计算同步；不接收固定 offsets。
ReplaySyncStatus compute_replay_sync(const std::vector<std::string>& locked_cameras,
                                     const std::map<std::string, long long>& actual_utc_ms,
                                     bool timebase_calibrated, int threshold_ms);

// 用户 seek 的请求世代与陈旧回调丢弃。
class SeekController {
public:
    struct State {
        long long generation = 0;
        long long target_ms = 0;
        long long position_ms = 0;
        bool reached = false;
        bool locating = false;
    };

    explicit SeekController(long long tolerance_ms = 1000);

    // 发起 seek：递增 request generation，返回新世代。
    long long request_seek(long long target_utc_ms);
    // 回放位置回调：世代不匹配 -> 返回 false（陈旧，丢弃且不更新位置）；
    // 匹配 -> 更新实际位置，达到目标容差才置 reached。
    bool on_position(long long generation, long long actual_utc_ms);
    State state() const { return st_; }
    long long tolerance_ms() const { return tol_; }
    void reset();

private:
    long long tol_;
    State st_;
};

}  // namespace core
}  // namespace railhmi
