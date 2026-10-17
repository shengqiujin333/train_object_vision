#include "railhmi/media/display_distributor.h"

#include <gst/gst.h>

#include <utility>

namespace railhmi {
namespace media {

RenderPathStatus probe_render_path(RenderPath requested) {
    RenderPathStatus st;
    st.path = requested;
    if (requested == RenderPath::Cpu) {
        st.available = true;
        return st;
    }
    // gl：需要 qmlglsink + glupload + glcolorconvert；缺失则显式不可用。
    gst_init(nullptr, nullptr);
    auto has = [](const char* name) {
        GstElementFactory* f = gst_element_factory_find(name);
        if (f) {
            gst_object_unref(f);
            return true;
        }
        return false;
    };
    const bool sink = has("qmlglsink");
    const bool upload = has("glupload");
    const bool cc = has("glcolorconvert");
    st.available = sink && upload && cc;
    if (!st.available) {
        std::string miss;
        auto add = [&](bool present, const char* n) {
            if (!present) {
                if (!miss.empty()) miss += ",";
                miss += n;
            }
        };
        add(sink, "qmlglsink");
        add(upload, "glupload");
        add(cc, "glcolorconvert");
        st.reason = "missing:" + miss;
    }
    return st;
}

DisplayDistributor::DisplayDistributor(std::string camera_id, RenderPath path)
    : camera_id_(std::move(camera_id)), path_(path) {}

void DisplayDistributor::update(const FrameLease& lease, bool presentable) {
    if (!lease.valid()) {
        std::lock_guard<std::mutex> lk(mtx_);
        presentable_ = false;
        return;
    }
    if (path_ == RenderPath::Gl) {
        // gl 路径未接入（依赖缺失时不可用）；不静默降级为 cpu。
        std::lock_guard<std::mutex> lk(mtx_);
        presentable_ = false;
        last_error_ = "gl path not implemented";
        return;
    }

    BgrImage bgr;
    std::string err;
    if (!nv12_to_bgr(lease, &bgr, &err)) {
        std::lock_guard<std::mutex> lk(mtx_);
        ++convert_failures_;
        last_error_ = err;
        presentable_ = false;
        return;
    }
    FrameDescriptor d = lease.descriptor();
    std::lock_guard<std::mutex> lk(mtx_);
    snap_.camera_id = camera_id_;
    snap_.generation = d.generation;
    snap_.seq = d.seq;
    snap_.capture_mono_us = d.capture_mono_us;
    snap_.capture_wall_ms = d.capture_wall_ms;
    snap_.width = bgr.width;
    snap_.height = bgr.height;
    snap_.stride = static_cast<std::size_t>(bgr.width) * 3u;
    snap_.format = core::PixelFormat::Bgr888;
    snap_.data = std::move(bgr.data);
    snap_.presentable = presentable;
    has_ = true;
    presentable_ = presentable;
    last_error_.clear();
    ++updates_;
}

bool DisplayDistributor::snapshot(FrameSnapshot* out) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!has_ || snap_.data.empty()) return false;
    if (out) *out = snap_;
    return true;
}

bool DisplayDistributor::presentable() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return presentable_;
}

std::uint64_t DisplayDistributor::updates() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return updates_;
}

std::uint64_t DisplayDistributor::convert_failures() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return convert_failures_;
}

const std::string& DisplayDistributor::last_error() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return last_error_;
}

}  // namespace media
}  // namespace railhmi
