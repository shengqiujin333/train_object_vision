#include "railhmi/media/frame_lease.h"
#include "railhmi/media/frame_store.h"

#include <gst/gst.h>
#include <gst/video/video.h>

#include <cstring>
#include <mutex>
#include <utility>

namespace railhmi {
namespace media {

namespace {

std::string normalize_colorimetry(const gchar* raw) {
    if (!raw) return "unknown";
    const std::string s(raw);
    if (s.find("bt709") != std::string::npos) return "bt709_full";
    if (s.find("bt601") != std::string::npos || s.find("smpte170m") != std::string::npos ||
        s.find("bt470") != std::string::npos) {
        return "bt601_full";
    }
    return "unknown";
}

}  // namespace

// 底层帧缓冲：持有 GstSample（ref 共享所有权）与映射后的 GstVideoFrame。
// 最后一个 shared_ptr 销毁时 unmap/unref 并触发 release 回调。
class FrameBuffer {
public:
    FrameBuffer(std::string camera_id) { descriptor_.camera_id = std::move(camera_id); }
    ~FrameBuffer() {
        if (mapped_) gst_video_frame_unmap(&vf_);
        if (sample_) gst_sample_unref(sample_);
        if (release_) release_(descriptor_);
    }

    FrameDescriptor descriptor_;
    GstSample* sample_ = nullptr;
    GstVideoFrame vf_{};
    bool mapped_ = false;
    FrameStore::ReleaseCallback release_;
};

const FrameDescriptor& FrameLease::descriptor() const {
    static const FrameDescriptor kEmpty;
    return buffer_ ? buffer_->descriptor_ : kEmpty;
}

std::size_t FrameLease::plane_count() const {
    return buffer_ ? buffer_->descriptor_.layout.planes.size() : 0;
}

const std::uint8_t* FrameLease::plane_data(std::size_t index) const {
    if (!buffer_ || !buffer_->mapped_) return nullptr;
    if (index >= buffer_->descriptor_.layout.planes.size()) return nullptr;
    return static_cast<const std::uint8_t*>(buffer_->vf_.data[index]);
}

long FrameLease::use_count() const { return buffer_.use_count(); }

struct FrameStore::Impl {
    std::string camera_id;
    FrameStore::ReleaseCallback cb;
    mutable std::mutex mtx;
    std::shared_ptr<FrameBuffer> latest;
    std::uint64_t published = 0;
    std::uint64_t rejected = 0;
    std::uint64_t ref_counter = 0;
};

FrameStore::FrameStore(std::string camera_id) : impl_(new Impl()) {
    impl_->camera_id = std::move(camera_id);
}

FrameStore::~FrameStore() = default;

void FrameStore::set_release_callback(ReleaseCallback cb) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->cb = std::move(cb);
}

core::LayoutStatus FrameStore::publish(GstSample* sample, const core::FrameMeta& meta,
                                       const std::string& colorimetry, const std::string& origin,
                                       std::string* reject_reason) {
    auto reject = [&](core::LayoutStatus st, const std::string& why) {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->rejected++;
        if (reject_reason) *reject_reason = why;
        return st;
    };

    if (!sample) return reject(core::LayoutStatus::UnsupportedFormat, "null sample");
    GstCaps* caps = gst_sample_get_caps(sample);
    if (!caps) return reject(core::LayoutStatus::UnsupportedFormat, "sample has no caps");
    GstVideoInfo info;
    if (!gst_video_info_from_caps(&info, caps)) {
        return reject(core::LayoutStatus::UnsupportedFormat, "caps not a video format");
    }

    core::PixelFormat fmt;
    switch (GST_VIDEO_INFO_FORMAT(&info)) {
        case GST_VIDEO_FORMAT_NV12: fmt = core::PixelFormat::Nv12; break;
        case GST_VIDEO_FORMAT_BGR: fmt = core::PixelFormat::Bgr888; break;
        case GST_VIDEO_FORMAT_RGB: fmt = core::PixelFormat::Rgb888; break;
        default: {
            const gchar* n = gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&info));
            return reject(core::LayoutStatus::UnsupportedFormat,
                          std::string("unsupported format ") + (n ? n : "?"));
        }
    }

    GstBuffer* buf = gst_sample_get_buffer(sample);
    if (!buf) return reject(core::LayoutStatus::UnsupportedFormat, "sample has no buffer");

    core::FrameLayout layout;
    core::LayoutStatus st;
    if (fmt == core::PixelFormat::Nv12) {
        st = core::make_nv12_layout(info.width, info.height, info.stride[0], info.stride[1],
                                    info.offset[1], &layout);
    } else {
        st = core::make_packed_layout(fmt, info.width, info.height, info.stride[0], &layout);
    }
    if (st != core::LayoutStatus::Ok) return reject(st, core::to_string(st));

    const std::uint64_t buf_size = gst_buffer_get_size(buf);
    if (layout.buffer_bytes > buf_size) {
        return reject(core::LayoutStatus::ExceedsBuffer,
                      "layout " + std::to_string(layout.buffer_bytes) + " > buffer " +
                          std::to_string(buf_size));
    }
    layout.buffer_bytes = buf_size;

    const GstStructure* st_caps = gst_caps_get_structure(caps, 0);
    std::string color = normalize_colorimetry(
        st_caps ? gst_structure_get_string(st_caps, "colorimetry") : nullptr);
    if (color == "unknown" && !colorimetry.empty()) color = colorimetry;

    auto fb = std::make_shared<FrameBuffer>(impl_->camera_id);
    fb->sample_ = gst_sample_ref(sample);  // 共享所有权（确保 buffer 在映射期间存活）
    if (!gst_video_frame_map(&fb->vf_, &info, buf, GST_MAP_READ)) {
        return reject(core::LayoutStatus::UnsupportedFormat, "gst_video_frame_map failed");
    }
    fb->mapped_ = true;
    fb->descriptor_.origin = origin.empty() ? "live" : origin;
    fb->descriptor_.session_id = meta.id.session_id;
    fb->descriptor_.generation = meta.id.generation;
    fb->descriptor_.seq = meta.id.seq;
    fb->descriptor_.capture_mono_us = meta.capture_mono_us;
    fb->descriptor_.capture_wall_ms = meta.capture_wall_ms;
    fb->descriptor_.pts_ns = meta.pts_ns;
    fb->descriptor_.layout = layout;
    fb->descriptor_.colorimetry = color;
    fb->descriptor_.memory_kind = "cpu";

    std::shared_ptr<FrameBuffer> old;
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        fb->descriptor_.buffer_ref = "fr-" + impl_->camera_id + "-" + std::to_string(++impl_->ref_counter);
        fb->release_ = impl_->cb;
        old = std::move(impl_->latest);
        impl_->latest = fb;
        impl_->published++;
    }
    // old 在锁外析构：若无消费者持有则触发 release 回调。
    return core::LayoutStatus::Ok;
}

FrameLease FrameStore::latest() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    if (!impl_->latest) return FrameLease();
    return FrameLease(impl_->latest);
}

const std::string& FrameStore::camera_id() const { return impl_->camera_id; }

std::uint64_t FrameStore::published() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->published;
}

std::uint64_t FrameStore::rejected() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->rejected;
}

}  // namespace media
}  // namespace railhmi
