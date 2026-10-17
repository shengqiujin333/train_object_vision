#pragma once

// railhmi-core: 压缩包滚动缓冲（视频 ITEM-011 纯逻辑）。
//
// 保存最近的**解码前压缩包**（depay/parse 之后、解码之前的 access unit），
// 按**字节与时长双上限**受控：超出时长上限淘汰最旧包，超出字节上限继续淘汰最旧包。
// 不使用显示/推理的丢帧策略；只保留最近窗口，不保存全部原始帧。
// 供报警保护片段的 pre-roll 窗口固化（ITEM-012）与录像缺口判定使用。
//
// 仅依赖 C++ 标准库；不含 GStreamer/像素处理。媒体层把 GstBuffer 映射为 CompressedPacket 后入队。

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace railhmi {
namespace core {

// 单个压缩包：时间戳（单调毫秒）+ 关键帧标志 + 压缩负载（access unit 字节）。
struct CompressedPacket {
    int64_t ts_ms = 0;
    bool keyframe = false;
    std::vector<std::uint8_t> data;
};

struct CompressedRingStats {
    std::size_t packets = 0;
    std::size_t bytes = 0;
    int64_t first_ts_ms = -1;
    int64_t last_ts_ms = -1;
    int64_t span_ms = 0;             // last - first（空为 0）
    std::size_t keyframes = 0;
    std::uint64_t pushed = 0;
    std::uint64_t evicted_duration = 0;   // 因时长上限淘汰的包数
    std::uint64_t evicted_bytes = 0;      // 因字节上限淘汰的包数
};

class CompressedRingBuffer {
public:
    // max_bytes<=0 表示不限字节；max_duration_ms<=0 表示不限时长。
    CompressedRingBuffer(std::size_t max_bytes, int max_duration_ms);
    void set_limits(std::size_t max_bytes, int max_duration_ms);

    // 追加一个压缩包（假定 ts_ms 单调不减）；返回本次淘汰的包数（0=未淘汰）。
    std::size_t push(CompressedPacket p);

    void clear();

    std::size_t max_bytes() const { return max_bytes_; }
    int max_duration_ms() const { return max_duration_ms_; }
    std::size_t size() const { return q_.size(); }
    std::size_t bytes() const { return bytes_; }
    bool empty() const { return q_.empty(); }
    int64_t first_ts_ms() const { return q_.empty() ? -1 : q_.front().ts_ms; }
    int64_t last_ts_ms() const { return q_.empty() ? -1 : q_.back().ts_ms; }
    int64_t span_ms() const { return q_.empty() ? 0 : (q_.back().ts_ms - q_.front().ts_ms); }

    CompressedRingStats stats() const;

    // [from_ms, to_ms] 闭合区间内的包（按原始顺序）；供片段固化/导出。
    std::vector<CompressedPacket> collect(int64_t from_ms, int64_t to_ms) const;

    // 返回 >= from_ms 的首个关键帧时间戳；无则 -1（片段起点对齐关键帧解码依赖）。
    int64_t first_keyframe_at_or_after(int64_t from_ms) const;

private:
    std::deque<CompressedPacket> q_;
    std::size_t max_bytes_;
    int max_duration_ms_;
    std::size_t bytes_ = 0;
    std::uint64_t pushed_ = 0;
    std::uint64_t evicted_duration_ = 0;
    std::uint64_t evicted_bytes_ = 0;
};

}  // namespace core
}  // namespace railhmi
