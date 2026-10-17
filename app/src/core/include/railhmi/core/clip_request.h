#pragma once

// railhmi-core: 保护片段请求、幂等键与事件时间窗级可用性（视频 ITEM-012 / 接口契约 §8 IF-07）。
//
// ClipRequest 扩展字段：request_id/event_id/camera_id/trigger_wall_ms/t0_wall_ms/t1_wall_ms；
// t0=trigger-pre×1000、t1=trigger+post×1000（默认 -30s/+60s，来自 thresholds.yaml）。
// request_id 为幂等键：相同键相同参数返回同一作业，不同参数 CONFLICT。
// 可用性三态 Complete/Incomplete/Unavailable，不用 bool record_available 冒充。
// 纯逻辑 + stdlib；媒体固化（真实文件）由 media::ClipWriter 完成。

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "railhmi/core/persistence_model.h"

namespace railhmi {
namespace core {

struct ClipRequest {
    std::string request_id;        // 幂等键
    long long event_id = -1;       // 已提交 DB id；-1=无
    std::string camera_id;
    long long trigger_wall_ms = 0;
    long long t0_wall_ms = 0;      // trigger - pre×1000
    long long t1_wall_ms = 0;      // trigger + post×1000
    long long duration_ms() const { return t1_wall_ms - t0_wall_ms; }
};

// 由 trigger 与 pre/post 秒数计算请求窗口。
ClipRequest make_clip_request(const std::string& request_id, long long event_id,
                              const std::string& camera_id, long long trigger_wall_ms,
                              int pre_seconds, int post_seconds);

// 幂等参数等价：event_id/camera_id/trigger/t0/t1 全部一致。
bool clip_request_same_params(const ClipRequest& a, const ClipRequest& b);

enum class ClipRequestOutcome { Added, Duplicate, Conflict };
const char* to_string(ClipRequestOutcome o);

// 幂等注册表。单写线程使用（事件服务），非线程安全。
class ClipRequestRegistry {
public:
    // 首次 -> Added；同 request_id 同参数 -> Duplicate；同 request_id 不同参数 -> Conflict。
    ClipRequestOutcome submit(const ClipRequest& r);
    bool known(const std::string& request_id) const;
    const ClipRequest* find(const std::string& request_id) const;
    std::size_t size() const { return jobs_.size(); }

private:
    std::map<std::string, ClipRequest> jobs_;
};

// 事件时间窗级录像可用性三态。
enum class ClipAvailability { Complete, Incomplete, Unavailable };
const char* to_string(ClipAvailability a);
bool parse_clip_availability(const std::string& s, ClipAvailability* out);

// 固化结果：请求窗口 + 实际媒体窗口 + 可用性 + 缺口。
struct ClipResult {
    std::string request_id;
    std::string camera_id;
    long long requested_t0_ms = 0;
    long long requested_t1_ms = 0;
    long long actual_t0_ms = 0;    // 关键帧对齐后的实际起点
    long long actual_t1_ms = 0;    // 实际末包时刻
    ClipAvailability availability = ClipAvailability::Unavailable;
    bool published = false;        // 事务索引发布成功
    long long size_bytes = 0;
    std::string path;
    std::string reason;            // 失败/缺口原因（稳定 token，可空）
    bool has_gap = false;
    RecordingGapRow gap;
};

}  // namespace core
}  // namespace railhmi
