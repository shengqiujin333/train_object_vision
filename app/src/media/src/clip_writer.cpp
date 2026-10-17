#include "railhmi/media/clip_writer.h"

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

#include "railhmi/platform/file_store.h"

namespace railhmi {
namespace media {

namespace {

long long mono_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

std::string gst_quote(const std::string& v) {
    std::string o;
    o.reserve(v.size() + 2);
    for (char c : v) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

// 稳定且安全的文件名片段：仅保留 [A-Za-z0-9._-]，其余替换为 '_'。
std::string sanitize(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        o += ok ? c : '_';
    }
    return o;
}

bool fsync_path(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    const int rc = ::fsync(fd);
    ::close(fd);
    return rc == 0;
}

bool fsync_dir(const std::string& dir) {
    const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) return false;
    const int rc = ::fsync(fd);
    ::close(fd);
    return rc == 0;
}

long long file_size(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return -1;
    return static_cast<long long>(st.st_size);
}

// 等待管线到达 EOS；ERROR/超时返回 false 并置 err。
bool drain_until_eos(GstElement* pipeline, int timeout_ms, std::string* err) {
    GstBus* bus = gst_element_get_bus(pipeline);
    const long long deadline = mono_ms() + timeout_ms;
    bool eos = false, error = false;
    while (!eos && !error) {
        const long long remain = deadline - mono_ms();
        if (remain <= 0) break;
        GstMessage* msg = gst_bus_timed_pop_filtered(
            bus, static_cast<GstClockTime>(remain) * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        if (!msg) break;
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            GError* e = nullptr;
            gchar* dbg = nullptr;
            gst_message_parse_error(msg, &e, &dbg);
            if (err) *err = e ? e->message : "gst error";
            if (e) g_error_free(e);
            g_free(dbg);
            error = true;
        } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
            eos = true;
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);
    if (!eos && !error && err) *err = "timeout waiting for EOS";
    return eos && !error;
}

std::string caps_for(const std::string& codec) {
    if (codec == "h265" || codec == "hevc")
        return "video/x-h265,stream-format=byte-stream,alignment=au";
    return "video/x-h264,stream-format=byte-stream,alignment=au";
}

std::string parse_for(const std::string& codec) {
    return (codec == "h265" || codec == "hevc") ? "h265parse" : "h264parse";
}

}  // namespace

const char* clip_container_name(ClipContainer c) {
    return c == ClipContainer::Matroska ? "matroska" : "mpegts";
}

const char* clip_container_extension(ClipContainer c) {
    return c == ClipContainer::Matroska ? "mkv" : "ts";
}

ClipWriter::ClipWriter(ClipWriterConfig cfg) : cfg_(std::move(cfg)) {}

std::string ClipWriter::final_path(const core::ClipRequest& req) const {
    std::string dir = cfg_.output_dir;
    if (!dir.empty() && dir.back() == '/') dir.pop_back();
    return dir + "/" + sanitize(req.camera_id) + "_" + sanitize(req.request_id) + "." +
           clip_container_extension(cfg_.container);
}

std::string ClipWriter::clip_path(const core::ClipRequest& req) const { return final_path(req); }

std::string ClipWriter::temp_path(const core::ClipRequest& req) const {
    return final_path(req) + ".part";
}

core::ClipResult ClipWriter::cfg_fail(const core::ClipRequest& req, const std::string& reason) {
    core::ClipResult r;
    r.request_id = req.request_id;
    r.camera_id = req.camera_id;
    r.requested_t0_ms = req.t0_wall_ms;
    r.requested_t1_ms = req.t1_wall_ms;
    r.availability = core::ClipAvailability::Unavailable;
    r.reason = reason;
    r.has_gap = true;
    r.gap.cam = req.camera_id;
    r.gap.t0_ms = req.t0_wall_ms;
    r.gap.t1_ms = req.t1_wall_ms;
    r.gap.reason = reason;
    last_ = r;
    return r;
}

core::ClipResult ClipWriter::record_unavailable(const core::ClipRequest& req,
                                                const std::string& reason) {
    return cfg_fail(req, reason);
}

bool ClipWriter::mux_and_validate(const core::ClipRequest& /*req*/,
                                  const std::vector<core::CompressedPacket>& packets,
                                  const std::string& codec, const std::string& tmp,
                                  long long* size_bytes, std::string* err) {
    gst_init(nullptr, nullptr);
    const std::string caps = caps_for(codec);
    const std::string parse = parse_for(codec);
    const std::string mux = (cfg_.container == ClipContainer::Matroska) ? "matroskamux" : "mpegtsmux";

    std::string desc = "appsrc name=clip_src is-live=false format=time block=true ! " + caps +
                       " ! " + parse + " ! " + mux + " ! filesink location=\"" + gst_quote(tmp) + "\"";
    GError* gerr = nullptr;
    GstElement* pipeline = gst_parse_launch(desc.c_str(), &gerr);
    if (gerr) {
        if (err) *err = std::string("mux pipeline: ") + gerr->message;
        g_error_free(gerr);
        if (pipeline) gst_object_unref(pipeline);
        return false;
    }
    if (!pipeline) { if (err) *err = "mux pipeline build failed"; return false; }

    GstElement* src = gst_bin_get_by_name(GST_BIN(pipeline), "clip_src");
    if (!src) {
        gst_object_unref(pipeline);
        if (err) *err = "appsrc missing";
        return false;
    }
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    const long long base = packets.front().ts_ms;
    bool push_ok = true;
    for (const auto& p : packets) {
        GstBuffer* buf = gst_buffer_new_allocate(nullptr, p.data.size(), nullptr);
        GstMapInfo mi;
        if (gst_buffer_map(buf, &mi, GST_MAP_WRITE)) {
            std::memcpy(mi.data, p.data.data(), p.data.size());
            gst_buffer_unmap(buf, &mi);
        }
        const GstClockTime pts =
            static_cast<GstClockTime>(std::max<long long>(0, p.ts_ms - base)) * GST_MSECOND;
        GST_BUFFER_PTS(buf) = pts;
        GST_BUFFER_DTS(buf) = pts;
        if (gst_app_src_push_buffer(GST_APP_SRC(src), buf) != GST_FLOW_OK) {
            push_ok = false;
            break;
        }
    }
    if (push_ok) gst_app_src_end_of_stream(GST_APP_SRC(src));

    const bool mux_eos = drain_until_eos(pipeline, cfg_.mux_timeout_ms, err);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(src);
    gst_object_unref(pipeline);

    if (!push_ok) { if (err) *err = "appsrc push failed"; return false; }
    if (!mux_eos) return false;

    const long long sz = file_size(tmp);
    if (sz <= 0) { if (err) *err = "empty muxed file"; return false; }
    if (size_bytes) *size_bytes = sz;

    if (cfg_.validate) {
        const std::string demux = (cfg_.container == ClipContainer::Matroska) ? "matroskademux" : "tsdemux";
        std::string vdesc = "filesrc location=\"" + gst_quote(tmp) + "\" ! " + demux +
                            " ! fakesink sync=false";
        GError* verr = nullptr;
        GstElement* vpipe = gst_parse_launch(vdesc.c_str(), &verr);
        if (verr) {
            if (err) *err = std::string("validate pipeline: ") + verr->message;
            g_error_free(verr);
            if (vpipe) gst_object_unref(vpipe);
            return false;
        }
        if (!vpipe) { if (err) *err = "validate pipeline build failed"; return false; }
        gst_element_set_state(vpipe, GST_STATE_PLAYING);
        std::string verrmsg;
        const bool vok = drain_until_eos(vpipe, cfg_.mux_timeout_ms, &verrmsg);
        gst_element_set_state(vpipe, GST_STATE_NULL);
        gst_object_unref(vpipe);
        if (!vok) {
            if (err) *err = "validate failed: " + verrmsg;
            return false;
        }
    }
    return true;
}

core::ClipResult ClipWriter::solidify(const core::ClipRequest& req,
                                      const std::vector<core::CompressedPacket>& packets,
                                      const std::string& codec, const ClipPublisher& publish) {
    if (packets.empty()) return record_unavailable(req, "NO_MEDIA");
    if (cfg_.output_dir.empty()) return cfg_fail(req, "NO_OUTPUT_DIR");

    std::string err;
    if (!platform::FileStore::ensure_parent_dir(final_path(req), &err)) return cfg_fail(req, "MKDIR_FAILED");

    core::ClipResult r;
    r.request_id = req.request_id;
    r.camera_id = req.camera_id;
    r.requested_t0_ms = req.t0_wall_ms;
    r.requested_t1_ms = req.t1_wall_ms;
    r.actual_t0_ms = packets.front().ts_ms;   // 已关键帧对齐的实际起点
    r.actual_t1_ms = packets.back().ts_ms;

    // 覆盖判定（事件时间窗级三态）：关键帧对齐使 actual_t0 可略大于 requested_t0，容差内视为完整。
    const long long tol = cfg_.coverage_tolerance_ms;
    const bool pre_ok = r.actual_t0_ms <= req.t0_wall_ms + tol;
    const bool post_ok = r.actual_t1_ms >= req.t1_wall_ms - tol;
    r.availability = (pre_ok && post_ok) ? core::ClipAvailability::Complete
                                         : core::ClipAvailability::Incomplete;
    const long long miss_pre = r.actual_t0_ms - req.t0_wall_ms;   // >tol = 前录不足
    const long long miss_post = req.t1_wall_ms - r.actual_t1_ms;  // >tol = 后录不足
    if (!pre_ok || !post_ok) {
        r.has_gap = true;
        r.gap.cam = req.camera_id;
        if (!pre_ok && !post_ok) {
            // 两端都不足：取缺口较大一侧为主区间，reason 标注双侧。
            if (miss_pre >= miss_post) {
                r.gap.t0_ms = req.t0_wall_ms;
                r.gap.t1_ms = r.actual_t0_ms;
            } else {
                r.gap.t0_ms = r.actual_t1_ms;
                r.gap.t1_ms = req.t1_wall_ms;
            }
            r.gap.reason = "PRE_POST_ROLL_SHORTAGE";
        } else if (!pre_ok) {
            r.gap.t0_ms = req.t0_wall_ms;
            r.gap.t1_ms = r.actual_t0_ms;
            r.gap.reason = "PRE_ROLL_SHORTAGE";
        } else {
            r.gap.t0_ms = r.actual_t1_ms;
            r.gap.t1_ms = req.t1_wall_ms;
            r.gap.reason = "POST_ROLL_SHORTAGE";
        }
        r.reason = r.gap.reason;
    }

    const std::string tmp = temp_path(req);
    std::remove(tmp.c_str());  // 清除可能残留的孤儿临时文件
    long long size_bytes = 0;
    if (!mux_and_validate(req, packets, codec, tmp, &size_bytes, &err)) {
        std::remove(tmp.c_str());
        core::ClipResult f = cfg_fail(req, "MUX_FAILED");
        f.actual_t0_ms = r.actual_t0_ms;
        f.actual_t1_ms = r.actual_t1_ms;
        f.reason = "MUX_FAILED:" + err;
        f.gap.reason = "MUX_FAILED";
        last_ = f;
        return f;
    }

    // flush+fsync 文件 → rename → fsync 目录（同目录临时文件保证原子 rename）。
    if (!fsync_path(tmp)) {
        std::remove(tmp.c_str());
        core::ClipResult f = cfg_fail(req, "FSYNC_FAILED");
        last_ = f;
        return f;
    }
    if (::rename(tmp.c_str(), final_path(req).c_str()) != 0) {
        std::remove(tmp.c_str());
        core::ClipResult f = cfg_fail(req, "RENAME_FAILED");
        last_ = f;
        return f;
    }
    {
        std::string dir = cfg_.output_dir;
        if (!dir.empty() && dir.back() == '/') dir.pop_back();
        if (!fsync_dir(dir)) {
            // 目录 fsync 失败不删除已 rename 的媒体；登记原因（索引仍尝试发布）。
            r.reason = r.reason.empty() ? "DIR_FSYNC_FAILED" : r.reason + ";DIR_FSYNC_FAILED";
        }
    }

    r.path = final_path(req);
    r.size_bytes = file_size(r.path);
    if (r.size_bytes <= 0) {
        core::ClipResult f = cfg_fail(req, "EMPTY_AFTER_RENAME");
        last_ = f;
        return f;
    }

    // 事务索引发布（DB 写入由事件/持久化层完成）；失败保留可恢复孤儿 + 日志。
    if (publish) {
        r.published = publish(r);
        if (!r.published) {
            r.reason = r.reason.empty() ? "INDEX_PUBLISH_FAILED" : r.reason + ";INDEX_PUBLISH_FAILED";
        }
    }
    last_ = r;
    return r;
}

}  // namespace media
}  // namespace railhmi
