#pragma once

// railhmi-media: 每相机“最新帧”仓库（视频 ITEM-004）。
// 发布 GstSample（ref 共享所有权并映射）→ 构建/校验 FrameDescriptor → 向消费者发放 FrameLease。
// 无帧/被替换后旧帧仍由已持有租约的消费者维持；最后一个租约释放时 unmap/unref 并触发回调。
// 本头不包含 GStreamer（GstSample 前向声明），便于上层无 GStreamer 编译单元使用。

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "railhmi/core/frame_identity.h"
#include "railhmi/core/frame_layout.h"
#include "railhmi/media/frame_lease.h"

struct _GstSample;
typedef struct _GstSample GstSample;

namespace railhmi {
namespace media {

class FrameStore {
public:
    using ReleaseCallback = std::function<void(const FrameDescriptor&)>;

    explicit FrameStore(std::string camera_id);
    ~FrameStore();

    FrameStore(const FrameStore&) = delete;
    FrameStore& operator=(const FrameStore&) = delete;

    void set_release_callback(ReleaseCallback cb);

    // 发布一帧：ref sample、按 caps 映射并构造布局；格式/stride 非法时拒绝且不替换当前帧。
    // 返回 core::LayoutStatus：Ok 表示已发布；其余为显式拒绝原因。
    core::LayoutStatus publish(GstSample* sample, const core::FrameMeta& meta,
                               const std::string& colorimetry, const std::string& origin,
                               std::string* reject_reason = nullptr);

    // 当前最新帧的租约；无帧时返回空租约（status()==NOT_READY）。线程安全。
    FrameLease latest() const;

    const std::string& camera_id() const;
    std::uint64_t published() const;
    std::uint64_t rejected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace media
}  // namespace railhmi
