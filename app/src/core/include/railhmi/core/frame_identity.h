#pragma once

// railhmi-core: 视频帧身份与跨重连代际（视频 ITEM-003）。
//
// 每帧携带跨重连身份 (session_id, camera_id, generation, seq) 与时间
// (capture_mono_us 单调接收时刻 / capture_wall_ms 已校准 UTC / pts_ns 相机 PTS)。
// generation 仅在会话**真正重建**时递增；seq 在同一 generation 内严格递增。
// 消费者侧接纳器拒绝旧 session/generation、重复 seq、乱序 seq。
//
// 纯标准库（可最小工具链单测）；不含 Qt/GStreamer/相机。

#include <cstdint>
#include <string>

namespace railhmi {
namespace core {

// 跨重连帧身份。session_id 为应用启动生成的不透明串，跨相机共享。
struct FrameId {
    std::string session_id;
    std::string camera_id;
    std::uint64_t generation = 0;
    std::uint64_t seq = 0;

    bool operator==(const FrameId& o) const {
        return session_id == o.session_id && camera_id == o.camera_id &&
               generation == o.generation && seq == o.seq;
    }
    bool same_stream(const FrameId& o) const {
        return session_id == o.session_id && camera_id == o.camera_id &&
               generation == o.generation;
    }
};

// 帧元数据。capture_wall_ms 未校准（或未提供 TimeBase）时为 -1；
// pts_ns 无效时为 -1。二者都不得当作单调或 UTC 的替代。
struct FrameMeta {
    FrameId id;
    std::int64_t capture_mono_us = 0;   // steady_clock 单调微秒（接收时刻）
    std::int64_t capture_wall_ms = -1;  // 已校准 UTC epoch 毫秒；-1=不可用
    std::int64_t pts_ns = -1;           // 相机/媒体 PTS；-1=无/无效
};

// 生成本次应用启动的不透明 session_id（跨相机共享）。
std::string make_session_id();

// 每相机帧序号生成器。generation 只在 begin_generation() 时递增，seq 自增。
class FrameSequencer {
public:
    FrameSequencer() = default;
    FrameSequencer(std::string session_id, std::string camera_id);

    // 会话真正重建：generation+1，seq 归零。返回新 generation。
    std::uint64_t begin_generation();

    // 生成下一帧元数据（同一 generation 内 seq 严格递增）。
    FrameMeta next(std::int64_t capture_mono_us, std::int64_t capture_wall_ms,
                   std::int64_t pts_ns);

    std::uint64_t generation() const { return generation_; }
    std::uint64_t last_seq() const { return seq_; }        // 已发出的最大 seq（0=尚未发出）
    const std::string& session_id() const { return session_id_; }
    const std::string& camera_id() const { return camera_id_; }
    void set_session_id(std::string s) { session_id_ = std::move(s); }

private:
    std::string session_id_;
    std::string camera_id_;
    std::uint64_t generation_ = 0;
    std::uint64_t seq_ = 0;
};

// 消费者侧接纳判定。
enum class FrameAdmission {
    Accepted,
    StaleSession,      // 与已接纳 session 不同（旧/未知 session）
    StaleGeneration,   // generation 小于已接纳 generation
    DuplicateSeq,      // 同 generation 内 seq 重复
    OutOfOrderSeq,     // 同 generation 内 seq 倒退（乱序/旧结果）
};
const char* to_string(FrameAdmission a);

// 每相机/每消费者的接纳器。首个 session 被采纳；换 session 视为旧会话拒绝。
class FrameGate {
public:
    explicit FrameGate(std::string expected_session_id = "");

    FrameAdmission admit(const FrameId& id);

    // 当前 generation 是否与已接纳代际一致（供 presentable 判定复用）。
    bool is_current(const FrameId& id) const;

    const std::string& session_id() const { return session_id_; }
    std::uint64_t generation() const { return generation_; }
    std::uint64_t last_seq() const { return last_seq_; }
    bool has_seq() const { return has_seq_; }
    void reset(std::string session_id = "");

private:
    std::string session_id_;
    std::uint64_t generation_ = 0;
    std::uint64_t last_seq_ = 0;
    bool has_seq_ = false;
};

}  // namespace core
}  // namespace railhmi
