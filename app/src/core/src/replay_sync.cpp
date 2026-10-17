#include "railhmi/core/replay_sync.h"

#include <cstdlib>

namespace railhmi {
namespace core {

ReplaySyncStatus compute_replay_sync(const std::vector<std::string>& locked_cameras,
                                     const std::map<std::string, long long>& actual_utc_ms,
                                     bool timebase_calibrated, int threshold_ms) {
    ReplaySyncStatus out;
    if (!timebase_calibrated) {
        out.available = false;
        out.reason = "UNCALIBRATED";
        return out;
    }
    std::vector<long long> positions;
    positions.reserve(locked_cameras.size());
    for (const auto& cam : locked_cameras) {
        auto it = actual_utc_ms.find(cam);
        if (it == actual_utc_ms.end()) {
            out.missing.push_back(cam);
            continue;
        }
        positions.push_back(it->second);
    }
    if (!out.missing.empty()) {
        out.available = false;
        out.reason = "MISSING_CLOCK";
        return out;
    }
    if (positions.size() < 2) {
        // 少于两路无从“同步”，但时钟有效：available 真、无偏差提示。
        out.available = true;
        out.in_sync = true;
        out.max_deviation_ms = 0;
        return out;
    }
    const SyncResult s = sync_status(positions, threshold_ms);
    out.available = true;
    out.in_sync = s.in_sync;
    out.max_deviation_ms = s.max_deviation_ms;
    out.prompt = s.prompt;
    return out;
}

SeekController::SeekController(long long tolerance_ms)
    : tol_(tolerance_ms < 0 ? 0 : tolerance_ms) {}

long long SeekController::request_seek(long long target_utc_ms) {
    st_.generation += 1;
    st_.target_ms = target_utc_ms;
    // 保留最近一次已接受的 position（陈旧回调不得改动它）；定位中。
    st_.reached = false;
    st_.locating = true;
    return st_.generation;
}

bool SeekController::on_position(long long generation, long long actual_utc_ms) {
    if (generation != st_.generation) return false;   // 陈旧回调：丢弃，不更新位置
    st_.position_ms = actual_utc_ms;
    if (std::llabs(actual_utc_ms - st_.target_ms) <= tol_) {
        st_.reached = true;
        st_.locating = false;
    } else {
        st_.reached = false;
        st_.locating = true;
    }
    return true;
}

void SeekController::reset() { st_ = State{}; }

}  // namespace core
}  // namespace railhmi
