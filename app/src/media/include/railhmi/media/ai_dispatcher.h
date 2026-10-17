#pragma once

// railhmi-media: AI 帧派发边界（视频 ITEM-010）。
//
// 按 ai_enabled/ai_profile 选择需要推理的相机；按 app.ai.inference_interval 降频；
// 向推理侧投递带 (session_id, camera_id, generation, seq) 关联键与 capture 时刻的帧租约；
// 队列有界、丢最旧并计数；ai_profile=none（左右两侧）永不入队。
// 本边界只做媒体侧派发，不实现模型推理。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "railhmi/core/bounded_queue.h"
#include "railhmi/media/frame_lease.h"

namespace railhmi {
namespace media {

// 推理侧消费的帧记录：携带身份关联键与帧租约（像素所有权在 media）。
struct AiFrame {
    std::string session_id;
    std::string camera_id;
    std::string profile;
    std::uint64_t generation = 0;
    std::uint64_t seq = 0;
    std::int64_t capture_mono_us = 0;
    std::int64_t capture_wall_ms = -1;
    FrameLease lease;
};

struct AiCameraSpec {
    std::string camera_id;
    bool ai_enabled = false;
    std::string ai_profile = "none";
};

// 多路 AI 帧派发路由器：每相机独立降频与有界队列。
class AiFrameRouter {
public:
    AiFrameRouter(std::vector<AiCameraSpec> specs, int inference_interval, std::size_t queue_depth);

    // 取流 worker 每帧调用（worker 线程）：仅 ai_enabled 相机降频入队。
    void on_frame(const FrameLease& lease);

    bool pop(const std::string& camera_id, AiFrame* out);  // 推理侧消费（非阻塞）
    bool enabled(const std::string& camera_id) const;
    std::size_t depth(const std::string& camera_id) const;
    std::uint64_t enqueued(const std::string& camera_id) const;
    std::uint64_t dropped(const std::string& camera_id) const;
    std::uint64_t skipped_interval(const std::string& camera_id) const;
    std::uint64_t rejected_disabled(const std::string& camera_id) const;
    std::uint64_t total_enqueued() const;

    int interval() const { return interval_; }
    std::size_t queue_depth() const { return depth_; }
    const std::vector<std::string>& ids() const { return ids_; }

private:
    struct Channel {
        std::string camera_id;
        std::string profile;
        bool enabled = false;
        std::uint64_t seen = 0;
        std::uint64_t enqueued = 0;
        std::uint64_t dropped = 0;
        std::uint64_t skipped = 0;
        std::uint64_t rejected = 0;
        std::unique_ptr<core::BoundedQueue<AiFrame>> queue;
        mutable std::mutex mtx;
    };
    Channel* find(const std::string& camera_id) const;

    int interval_;
    std::size_t depth_;
    std::vector<std::unique_ptr<Channel>> channels_;
    std::vector<std::string> ids_;
};

}  // namespace media
}  // namespace railhmi
