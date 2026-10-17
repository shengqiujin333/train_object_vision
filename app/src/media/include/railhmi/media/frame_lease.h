#pragma once

// railhmi-media: 不可变帧租约与缓冲区所有权（视频 ITEM-004，公开类型，GStreamer-free）。
//
// FrameLease 持有底层帧缓冲的共享所有权（std::shared_ptr<FrameBuffer>，内部由 GstSample 支撑）；
// 可拷贝共享给多个消费者，最后一份销毁时释放缓冲并触发 release 回调。
// FrameDescriptor 只携带元数据（身份/时间/平面布局/不透明 buffer_ref），可安全交给 core/上层。
// 像素地址只经 FrameLease::plane_data 暴露，且仅在租约存活期有效；绝不传已 unmap 的裸地址。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "railhmi/core/frame_identity.h"
#include "railhmi/core/frame_layout.h"

namespace railhmi {
namespace media {

// 与接口契约 interface.schema.json 的 FrameDescriptor 对齐（memory_kind 固定 cpu）。
struct FrameDescriptor {
    std::string origin = "live";        // live|file|simulation
    std::string session_id;
    std::string camera_id;
    std::uint64_t generation = 0;
    std::uint64_t seq = 0;
    std::int64_t capture_mono_us = 0;
    std::int64_t capture_wall_ms = -1;  // -1=未校准/不可用
    std::int64_t pts_ns = -1;           // -1=无/无效
    railhmi::core::FrameLayout layout;  // width/height/format/planes/buffer_bytes
    std::string colorimetry = "unknown";
    std::string buffer_ref;             // 进程内不透明查找键；不可跨进程解引用
    std::string memory_kind = "cpu";
};

class FrameBuffer;  // 内部实现（GStreamer-backed），定义在 .cpp

// 消费者持有的不可变帧租约。空租约 status()==NOT_READY。
class FrameLease {
public:
    FrameLease() = default;

    bool valid() const { return buffer_ != nullptr; }
    const char* status() const { return valid() ? "READY" : "NOT_READY"; }
    const FrameDescriptor& descriptor() const;

    std::size_t plane_count() const;
    // 平面数据指针；仅在租约存活期有效。越界或空租约返回 nullptr。
    const std::uint8_t* plane_data(std::size_t index) const;

    long use_count() const;  // 共享持有者数量（含本对象）

private:
    friend class FrameStore;
    explicit FrameLease(std::shared_ptr<FrameBuffer> b) : buffer_(std::move(b)) {}
    std::shared_ptr<FrameBuffer> buffer_;
};

}  // namespace media
}  // namespace railhmi
