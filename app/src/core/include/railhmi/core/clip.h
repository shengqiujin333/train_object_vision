#pragma once

// railhmi-core: 本地报警保护片段与录像缺口模型（ITEM-019 / DESKTOP §10、INTERACTION §5.2/PLY-008）。
// 每路有界滚动缓冲；报警时固化 [触发-pre, 触发+post] 为保护片段（时长为配置值），
// post 窗口结束后 finalize 为完整片段；容量/期限来自配置并**优先保留保护片段**（复用 apply_retention）；
// 任何录像中断写入缺口记录（复用 add_gap）。纯逻辑 + stdlib；文件原子写入由平台层 FileStore 完成。

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "railhmi/core/persistence_model.h"
#include "railhmi/core/retention.h"
#include "railhmi/core/rules.h"

namespace railhmi {
namespace core {

// 有界滚动缓冲（按时间戳），用于保留报警前窗口。
class RollingBuffer {
public:
    explicit RollingBuffer(std::size_t capacity = 0);
    void set_capacity(std::size_t capacity);
    void push(long long ts_ms);                 // 满则丢最旧
    void drop_before(long long ts_ms);          // 丢弃早于 ts 的帧
    std::size_t size() const { return q_.size(); }
    std::size_t capacity() const { return cap_; }
    bool empty() const { return q_.empty(); }
    long long earliest() const { return q_.empty() ? -1 : q_.front(); }
    long long latest() const { return q_.empty() ? -1 : q_.back(); }
    // [t0, t1] 闭合区间内的帧数。
    std::size_t count_in(long long t0_ms, long long t1_ms) const;

private:
    std::deque<long long> q_;
    std::size_t cap_;
};

struct ClipConfig {
    int pre_seconds = 30;      // 报警前窗口
    int post_seconds = 60;     // 报警后窗口
    double fps = 25.0;         // 估算缓冲深度
    std::size_t max_frames_per_channel = 0;   // 0 -> 由 fps*(pre+post)+裕量推导
};

enum class ClipState { Recording, Finalized };
const char* to_string(ClipState s);

struct ProtectedClip {
    long long id = 0;
    std::string cam;
    AlarmLevel level = AlarmLevel::L3;
    long long trigger_ms = 0;
    long long t0_ms = 0;                       // 触发 - pre
    long long t1_ms = 0;                       // 触发 + post
    ClipState state = ClipState::Recording;
    bool complete = false;
    int frame_count = 0;
    long long duration_ms() const { return t1_ms - t0_ms; }
    std::string path;                          // 最终文件名（原子重命名后）
    long long size_bytes = 0;
};

// 保护片段 JSON 清单（写入磁盘；实际视频复用媒体层，应用只固化保护标记与时间范围）。
std::string clip_manifest_json(const ProtectedClip& clip);
// 估算片段字节数（时长 × 码率 Mbps）。
long long estimate_clip_size(const ProtectedClip& clip, double mbps);

class ClipRecorder {
public:
    ClipRecorder(ClipConfig cfg, RetentionPolicy policy);

    void on_frame(const std::string& cam, long long ts_ms);          // 滚动缓冲
    ProtectedClip on_alarm(const std::string& cam, AlarmLevel level, long long trigger_ms);
    std::size_t finalize(long long now_ms);                          // t1 <= now -> 完整
    void on_recording_interrupt(const std::string& cam, long long t0_ms, long long t1_ms,
                                const std::string& reason = "recording_interrupted");

    const std::vector<ProtectedClip>& clips() const { return clips_; }
    const std::vector<RecordingGapRow>& gaps() const { return gaps_; }
    // 保护片段（Critical）作为片段行（供保留策略统一处理）。
    std::vector<RecordingSegmentRow> segments() const;
    // 追加普通（continuous）片段行，用于演示保护优先淘汰。
    void add_normal_segment(long long id, const std::string& cam, long long t0_ms, long long t1_ms,
                            long long size_bytes);

    // 按配置容量/期限执行保留：普通片段先淘汰，保护片段仅在仍超容量时淘汰（最旧优先）。
    RetentionResult apply_retention_now(long long now_ms);

    // 写回保护片段大小（finalize 后按码率估算）。
    bool set_clip_size(long long id, long long size_bytes);

    ClipConfig config() const { return cfg_; }
    RollingBuffer* buffer(const std::string& cam);                    // 可能为 nullptr

private:
    ClipConfig cfg_;
    RetentionPolicy policy_;
    long long next_id_ = 1;
    std::vector<ProtectedClip> clips_;
    std::vector<RecordingSegmentRow> normal_;
    std::vector<RecordingGapRow> gaps_;
    std::deque<std::pair<std::string, RollingBuffer>> buffers_;
    RollingBuffer* buf_for(const std::string& cam, bool create);
};

}  // namespace core
}  // namespace railhmi
