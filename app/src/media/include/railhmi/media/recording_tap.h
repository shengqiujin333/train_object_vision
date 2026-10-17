#pragma once

// railhmi-media: 录像压缩分支的滚动缓冲接入（视频 ITEM-011，target 专用）。
//
// 把解码前压缩分支（depay/parse 之后、mppvideodec 之前）的压缩包接入 core::CompressedRingBuffer，
// 按字节与时长双上限滚动保留最近数据；不使用显示/推理丢帧策略，不保存全部原始帧。
// 公开头不含 GStreamer：调用方把 GstBuffer 映射为 core::CompressedPacket 后 push。

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "railhmi/core/compressed_ring.h"
#include "railhmi/core/recording_source.h"

namespace railhmi {
namespace media {

class RecordingTap {
public:
    RecordingTap(std::size_t max_bytes, int max_duration_ms);

    void push(core::CompressedPacket p);              // 取流 worker 线程
    void clear();
    void set_limits(std::size_t max_bytes, int max_duration_ms);

    core::CompressedRingStats stats() const;
    std::vector<core::CompressedPacket> collect(int64_t from_ms, int64_t to_ms) const;
    int64_t first_keyframe_at_or_after(int64_t from_ms) const;

    std::size_t max_bytes() const;
    int max_duration_ms() const;

    void set_source_status(const core::RecordingSourceStatus& s);
    core::RecordingSourceStatus source_status() const;

private:
    mutable std::mutex mtx_;
    core::CompressedRingBuffer ring_;
    core::RecordingSourceStatus source_;
};

}  // namespace media
}  // namespace railhmi
