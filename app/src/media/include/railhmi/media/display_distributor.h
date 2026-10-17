#pragma once

// railhmi-media: 显示分发与纹理交接（视频 ITEM-009）。
//
// 向 GUI 提供不可变、拥有像素的显示快照（cpu 路径为 BGR888）与 presentable 状态；
// 渲染层只消费快照上传纹理，不接触 GstSample/裸地址。同一相机多视图共享同一分发器
// （单一来源）；页面切换只改变查询集合，不重建会话。gl 路径为可开启候选，依赖
// （qmlglsink/glupload/glcolorconvert）缺失时显式不可用，不静默降级。

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "railhmi/core/frame_layout.h"
#include "railhmi/media/frame_lease.h"
#include "railhmi/media/pipeline_factory.h"  // RenderPath
#include "railhmi/media/rga_convert.h"

namespace railhmi {
namespace media {

struct RenderPathStatus {
    RenderPath path = RenderPath::Cpu;
    bool available = false;
    std::string reason;  // 不可用时说明缺哪个依赖
};

// 渲染路径能力探测：cpu 恒可用；gl 需 qmlglsink/glupload/glcolorconvert。
RenderPathStatus probe_render_path(RenderPath requested);

// 不可变显示快照：拥有像素（cpu 路径 BGR888）。
struct FrameSnapshot {
    std::string camera_id;
    std::uint64_t generation = 0;
    std::uint64_t seq = 0;
    std::int64_t capture_mono_us = 0;
    std::int64_t capture_wall_ms = -1;
    int width = 0;
    int height = 0;
    std::size_t stride = 0;  // bytes/row
    railhmi::core::PixelFormat format = railhmi::core::PixelFormat::Bgr888;
    std::vector<std::uint8_t> data;  // owned
    bool presentable = false;
};

// 每相机显示分发：从帧租约生成不可变快照并维护 presentable。
class DisplayDistributor {
public:
    DisplayDistributor(std::string camera_id, RenderPath path);

    // 取流 worker 调用（每帧）：生成拥有像素的快照；失败只记错误、presentable=false。
    void update(const FrameLease& lease, bool presentable);

    bool snapshot(FrameSnapshot* out) const;  // GUI 线程
    bool presentable() const;
    std::uint64_t updates() const;
    std::uint64_t convert_failures() const;
    const std::string& last_error() const;
    const std::string& camera_id() const { return camera_id_; }
    RenderPath render_path() const { return path_; }

private:
    std::string camera_id_;
    RenderPath path_;
    mutable std::mutex mtx_;
    FrameSnapshot snap_;
    bool has_ = false;
    bool presentable_ = false;
    std::uint64_t updates_ = 0;
    std::uint64_t convert_failures_ = 0;
    std::string last_error_;
};

}  // namespace media
}  // namespace railhmi
