#pragma once

// railhmi-media: 每相机取流 worker（视频 ITEM-006，target 专用）。
//
// 每路一个专用线程：有限等待拉帧（单次 pull 默认 ≤200ms）、drain GstBus(ERROR/EOS/
// STATE_CHANGED) 与六态机事件、驱动 core 六态机；对外提供线程安全快照
// （stats / 最新租约 / presentable）。重建/断流/冻结请求由 worker 线程执行，
// 保证 CameraStream 只在 worker 线程被访问。

#include <memory>
#include <string>

#include "railhmi/media/camera_stream.h"
#include "railhmi/media/frame_lease.h"

namespace railhmi {
namespace media {

class CameraWorker {
public:
    explicit CameraWorker(CameraStreamConfig cfg, int pull_timeout_ms = 200);
    ~CameraWorker();

    CameraWorker(const CameraWorker&) = delete;
    CameraWorker& operator=(const CameraWorker&) = delete;

    bool start(std::string* err = nullptr);
    void stop();

    void request_restart();  // 真正重建：generation+1、seq 归零（worker 线程执行）
    void request_cut();      // FI-01 断流（worker 线程执行）
    void request_freeze();   // FI-02 冻结（worker 线程执行）
    void request_resume();   // 解除冻结、恢复出帧（worker 线程执行）

    // 线程安全快照。
    StreamStats stats() const;
    FrameLease latest_frame() const;
    bool is_presentable() const;

    long long iterations() const;
    long long events_total() const;
    long long freezes() const;
    long long reconnect_attempts() const;
    long long escalations() const;
    long long recoveries() const;
    long long bus_state_changes() const;
    int pull_timeout_ms() const;

    // 三类消费者有界队列（显示/推理/录像）（视频 ITEM-008）；线程安全。
    FrameDispatcher& dispatcher();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace media
}  // namespace railhmi
