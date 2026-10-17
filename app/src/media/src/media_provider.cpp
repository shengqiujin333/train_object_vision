#include "railhmi/media/media_provider.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
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

std::string base_name(const std::string& path) {
    const std::size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

long long file_size(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return -1;
    return static_cast<long long>(st.st_size);
}

bool fsync_path(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    const int rc = ::fsync(fd);
    ::close(fd);
    return rc == 0;
}

// 有界运行到 EOS；ERROR/超时返回 false。
bool run_to_eos(const std::string& desc, int timeout_ms, std::string* err) {
    GError* gerr = nullptr;
    GstElement* pipeline = gst_parse_launch(desc.c_str(), &gerr);
    if (gerr) { if (err) *err = gerr->message; g_error_free(gerr); }
    if (!pipeline) { if (err && err->empty()) *err = "pipeline build failed"; return false; }
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
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
        } else {
            eos = true;
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    if (!eos && !error && err) *err = "timeout";
    return eos && !error;
}

// 复制到目标（临时文件 → fsync → rename），返回大小。
bool copy_with_fsync(const std::string& src, const std::string& dst, long long* size, std::string* err) {
    if (!platform::FileStore::ensure_parent_dir(dst, err)) return false;
    const std::string part = dst + ".part";
    {
        std::ifstream in(src, std::ios::binary);
        if (!in) { if (err) *err = "open src failed"; return false; }
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) { if (err) *err = "open dst failed"; return false; }
        out << in.rdbuf();
        out.flush();
        if (!out) { if (err) *err = "write failed"; return false; }
    }
    if (!fsync_path(part)) { std::remove(part.c_str()); if (err) *err = "fsync failed"; return false; }
    if (::rename(part.c_str(), dst.c_str()) != 0) {
        std::remove(part.c_str());
        if (err) *err = "rename failed";
        return false;
    }
    const long long sz = file_size(dst);
    if (sz <= 0) { if (err) *err = "empty copy"; return false; }
    if (size) *size = sz;
    return true;
}

}  // namespace

LocalMediaProvider::LocalMediaProvider(std::vector<core::RecordingSegmentRow> segments,
                                       std::vector<core::RecordingGapRow> gaps, MediaProviderConfig cfg)
    : segments_(std::move(segments)), gaps_(std::move(gaps)), cfg_(std::move(cfg)) {}

const core::RecordingSegmentRow* LocalMediaProvider::find_segment(const std::string& path) const {
    for (const auto& s : segments_) {
        if (s.path == path) return &s;
    }
    return nullptr;
}

bool LocalMediaProvider::file_usable(const std::string& path, long long* size_bytes,
                                     std::string* err) const {
    const long long sz = file_size(path);
    if (sz < 0) { if (err) *err = "not found"; return false; }
    if (sz <= 0) { if (err) *err = "empty file"; return false; }
    if (size_bytes) *size_bytes = sz;
    // 真解码容器判定：filesrc → decodebin → fakesink 需到达 EOS。
    const std::string desc = "filesrc location=\"" + gst_quote(path) + "\" ! decodebin ! fakesink sync=false";
    std::string perr;
    if (!run_to_eos(desc, cfg_.verdict_timeout_ms, &perr)) {
        if (err) *err = "undecodable: " + perr;
        return false;
    }
    return true;
}

core::SegmentQueryResult LocalMediaProvider::query_segments(const core::ProviderRequest& req,
                                                            long long now_mono_us) {
    if (core::provider_deadline_expired(req, now_mono_us)) {
        core::SegmentQueryResult r;
        r.request_id = req.request_id;
        r.camera_id = req.camera_id;
        r.status = core::ProviderStatus::Error;
        r.reason = core::provider_reason::kTimeout;
        return r;
    }
    std::vector<core::SegmentRef> refs;
    for (const auto& s : segments_) {
        if (s.cam != req.camera_id) continue;
        if (s.t1_ms <= req.t0_ms || s.t0_ms >= req.t1_ms) continue;
        core::SegmentRef ref;
        ref.camera_id = s.cam;
        ref.t0_ms = s.t0_ms;
        ref.t1_ms = s.t1_ms;
        ref.path = s.path;
        ref.kind = s.kind;
        ref.size_bytes = s.size_bytes;
        ref.complete = s.complete;
        long long sz = 0;
        std::string err;
        ref.usable = file_usable(s.path, &sz, &err);
        if (ref.usable && sz > 0) ref.size_bytes = sz;
        refs.push_back(ref);
    }
    return core::classify_segments(req, refs, gaps_, true);
}

core::SegmentQueryResult LocalMediaProvider::query_gaps(const core::ProviderRequest& req,
                                                        long long now_mono_us) const {
    core::SegmentQueryResult r;
    r.request_id = req.request_id;
    r.camera_id = req.camera_id;
    r.window_t0_ms = req.t0_ms;
    r.window_t1_ms = req.t1_ms;
    if (core::provider_deadline_expired(req, now_mono_us)) {
        r.status = core::ProviderStatus::Error;
        r.reason = core::provider_reason::kTimeout;
        return r;
    }
    r.status = core::ProviderStatus::Ok;
    for (const auto& g : gaps_) {
        if (g.cam != req.camera_id) continue;
        if (g.t1_ms <= req.t0_ms || g.t0_ms >= req.t1_ms) continue;
        r.gaps.push_back(g);
    }
    return r;
}

core::PlaybackResult LocalMediaProvider::play_segment(const core::ProviderRequest& req,
                                                      const std::string& path, long long from_ms,
                                                      long long now_mono_us) {
    core::PlaybackResult r;
    r.request_id = req.request_id;
    r.requested_pos_ms = from_ms;
    if (core::provider_deadline_expired(req, now_mono_us)) {
        r.status = core::ProviderStatus::Error;
        r.reason = core::provider_reason::kTimeout;
        return r;
    }
    long long sz = 0;
    std::string err;
    if (!file_usable(path, &sz, &err)) {
        r.status = core::ProviderStatus::Unavailable;
        r.reason = core::provider_reason::kNotFound;
        return r;
    }
    const core::RecordingSegmentRow* seg = find_segment(path);
    const long long base = seg ? seg->t0_ms : 0;
    long long rel = from_ms - base;
    if (rel < 0) rel = 0;

    gst_init(nullptr, nullptr);
    const std::string desc = "filesrc location=\"" + gst_quote(path) +
                             "\" ! decodebin ! videoconvert ! appsink name=frame_sink sync=false";
    GError* gerr = nullptr;
    GstElement* pipeline = gst_parse_launch(desc.c_str(), &gerr);
    if (gerr) {
        r.status = core::ProviderStatus::Error;
        r.reason = core::provider_reason::kUnsupported;
        r.decoded_frames = 0;
        g_error_free(gerr);
        if (pipeline) gst_object_unref(pipeline);
        return r;
    }
    if (!pipeline) {
        r.status = core::ProviderStatus::Error;
        r.reason = core::provider_reason::kUnsupported;
        return r;
    }
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "frame_sink");
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // 等预滚（ASYNC_DONE）后再 seek，否则 seek 会被忽略（永远从 0 解码）。
    {
        GstBus* bus = gst_element_get_bus(pipeline);
        const long long d = mono_ms() + 3000;
        while (mono_ms() < d) {
            GstMessage* m = gst_bus_timed_pop_filtered(
                bus, static_cast<GstClockTime>(d - mono_ms()) * GST_MSECOND,
                static_cast<GstMessageType>(GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR));
            if (!m) break;
            const bool is_err = GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR;
            gst_message_unref(m);
            if (is_err) break;
            break;  // ASYNC_DONE -> preroll 完成
        }
        gst_object_unref(bus);
    }

    // 请求发出≠播放成功：seek 后以实际解码到的 PTS 作为 position。
    if (rel > 0) {
        gst_element_seek_simple(pipeline, GST_FORMAT_TIME,
                                static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                                static_cast<GstClockTime>(rel) * GST_MSECOND);
    }

    const long long wall_start_ms = mono_ms();
    const long long wall_deadline = wall_start_ms + cfg_.verdict_timeout_ms + cfg_.playback_media_ms * 2;
    long long first_pts = -1, last_pts = -1, frames = 0;
    bool timed_out = false;
    while (sink) {
        if (mono_ms() > wall_deadline) { timed_out = true; break; }
        if (core::provider_deadline_expired(req, now_mono_us + (mono_ms() - wall_start_ms) * 1000)) {
            timed_out = true;
            break;
        }
        GstSample* s = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 200 * GST_MSECOND);
        if (!s) {
            if (frames > 0) break;  // 已到达并播放一段
            continue;
        }
        GstBuffer* buf = gst_sample_get_buffer(s);
        if (buf && GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(buf))) {
            const long long pts_ms = static_cast<long long>(GST_BUFFER_PTS(buf) / GST_MSECOND);
            if (first_pts < 0) first_pts = pts_ms;
            last_pts = pts_ms;
            ++frames;
            if (last_pts - first_pts >= cfg_.playback_media_ms) { gst_sample_unref(s); break; }
        }
        gst_sample_unref(s);
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    if (sink) gst_object_unref(sink);
    gst_object_unref(pipeline);

    r.decoded_frames = frames;
    if (frames == 0) {
        r.status = core::ProviderStatus::Error;
        r.reason = timed_out ? core::provider_reason::kTimeout : core::provider_reason::kNotFound;
        return r;
    }
    r.reached = true;
    r.actual_pos_ms = base + (last_pts >= 0 ? last_pts : rel);
    r.status = core::ProviderStatus::Ok;
    return r;
}

core::ExportResult LocalMediaProvider::export_segment(const core::ProviderRequest& req,
                                                      const std::string& path, long long now_mono_us) {
    core::ExportResult r;
    r.request_id = req.request_id;
    if (core::provider_deadline_expired(req, now_mono_us)) {
        r.status = core::ProviderStatus::Error;
        r.reason = core::provider_reason::kTimeout;
        return r;
    }
    long long sz = 0;
    std::string err;
    if (!file_usable(path, &sz, &err)) {
        r.status = core::ProviderStatus::Unavailable;
        r.reason = core::provider_reason::kNotFound;
        return r;
    }
    if (cfg_.export_dir.empty()) {
        r.status = core::ProviderStatus::Error;
        r.reason = core::provider_reason::kStorageError;
        return r;
    }
    std::string dir = cfg_.export_dir;
    if (!dir.empty() && dir.back() == '/') dir.pop_back();
    const std::string dst = dir + "/" + base_name(path);
    long long out_size = 0;
    if (!copy_with_fsync(path, dst, &out_size, &err)) {
        r.status = core::ProviderStatus::Error;
        r.reason = core::provider_reason::kStorageError;
        return r;
    }
    r.path = dst;
    r.size_bytes = out_size;
    r.complete = true;
    r.status = core::ProviderStatus::Ok;
    return r;
}

std::string LocalMediaProvider::thumbnail(const std::string& path, long long at_ms,
                                          const std::string& name) {
    if (cfg_.thumbnail_dir.empty()) return "";
    long long sz = 0;
    std::string err;
    if (!file_usable(path, &sz, &err)) return "";
    platform::FileStore::ensure_parent_dir(cfg_.thumbnail_dir + "/x", &err);

    const core::RecordingSegmentRow* seg = find_segment(path);
    const long long base = seg ? seg->t0_ms : 0;
    long long rel = at_ms - base;
    if (rel < 0) rel = 0;

    const std::string stem = sanitize(name.empty() ? base_name(path) : name);
    const std::string out = cfg_.thumbnail_dir + "/" + stem + "_%05d.jpg";
    const std::string produced = cfg_.thumbnail_dir + "/" + stem + "_00000.jpg";
    std::remove(produced.c_str());

    gst_init(nullptr, nullptr);
    const std::string desc = "filesrc location=\"" + gst_quote(path) +
                             "\" ! decodebin ! videoconvert ! jpegenc ! multifilesink location=\"" +
                             gst_quote(out) + "\" max-files=1";
    GError* gerr = nullptr;
    GstElement* pipeline = gst_parse_launch(desc.c_str(), &gerr);
    if (gerr) g_error_free(gerr);
    if (!pipeline) return "";
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (rel > 0) {
        gst_element_seek_simple(pipeline, GST_FORMAT_TIME,
                                static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                                static_cast<GstClockTime>(rel) * GST_MSECOND);
    }
    const long long deadline = mono_ms() + cfg_.verdict_timeout_ms;
    while (mono_ms() < deadline) {
        if (file_size(produced) > 0) break;
        struct timespec ts { 0, 50 * 1000 * 1000 };
        nanosleep(&ts, nullptr);
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    if (file_size(produced) > 0) return produced;
    return "";
}

core::SegmentQueryResult NvrProvider::query_segments(const core::ProviderRequest& req) const {
    core::SegmentQueryResult r;
    r.request_id = req.request_id;
    r.camera_id = req.camera_id;
    r.window_t0_ms = req.t0_ms;
    r.window_t1_ms = req.t1_ms;
    r.source_configured = false;
    r.status = core::ProviderStatus::Unavailable;
    r.reason = core::provider_reason::kUnconfigured;
    return r;
}

core::PlaybackResult NvrProvider::play_segment(const core::ProviderRequest& req) const {
    core::PlaybackResult r;
    r.request_id = req.request_id;
    r.status = core::ProviderStatus::Unavailable;
    r.reason = core::provider_reason::kUnconfigured;
    return r;
}

core::ExportResult NvrProvider::export_segment(const core::ProviderRequest& req) const {
    core::ExportResult r;
    r.request_id = req.request_id;
    r.status = core::ProviderStatus::Unavailable;
    r.reason = core::provider_reason::kUnconfigured;
    return r;
}

}  // namespace media
}  // namespace railhmi
