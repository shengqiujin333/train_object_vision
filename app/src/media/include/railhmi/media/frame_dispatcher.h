#pragma once

// railhmi-media: 三类消费者的有界队列与背压（视频 ITEM-008）。
//
// 取流 worker 每帧调用 dispatch()，非阻塞分发到三类消费者：
//   * 显示  —— latest-only（容量 1），旧帧被替换即计入 display_dropped；
//   * 推理  —— 有界丢最旧（容量 = ai_queue_depth），计入 ai_dropped；
//   * 录像  —— 独立有界分支（条数 / 字节上限），溢出计入 recording_overflow（可映射为缺口）。
// UI/AI 慢只影响各自队列，不阻塞取流；录像分支不受显示/推理丢帧策略影响。
// 线程安全；公开头不包含 GStreamer。

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "railhmi/core/bounded_queue.h"
#include "railhmi/media/frame_lease.h"

namespace railhmi {
namespace media {

struct DispatcherConfig {
    std::size_t display_capacity = 1;
    std::size_t ai_capacity = 2;                      // app.ai.queue_depth
    std::size_t recording_capacity = 30;              // 录像分支条数上限
    std::size_t recording_bytes_capacity = 0;         // 0 = 只按条数
};

class FrameDispatcher {
public:
    explicit FrameDispatcher(DispatcherConfig cfg = DispatcherConfig());

    // 取流 worker 调用：非阻塞分发到三类消费者。
    void dispatch(const FrameLease& lease);

    // 消费者拉取（非阻塞）。
    bool display_latest(FrameLease* out) const;
    bool ai_pop(FrameLease* out);
    bool recording_pop(FrameLease* out);

    // 计数与深度。
    std::uint64_t dispatched() const;
    std::uint64_t display_dropped() const;
    std::uint64_t ai_dropped() const;
    std::uint64_t recording_overflow() const;
    std::size_t display_depth() const;
    std::size_t ai_depth() const;
    std::size_t recording_depth() const;
    std::size_t recording_bytes() const;

private:
    DispatcherConfig cfg_;
    core::BoundedQueue<FrameLease> display_;
    core::BoundedQueue<FrameLease> ai_;
    core::BoundedQueue<FrameLease> recording_;
    mutable std::mutex mtx_;
    std::uint64_t dispatched_ = 0;
};

}  // namespace media
}  // namespace railhmi
