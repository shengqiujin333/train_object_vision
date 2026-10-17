// railhmi-media: 视频源能力探测与配置绑定实现（视频 ITEM-001）。
// 目标专用：直接 gst-inspect-1.0 <element> + 原生 GStreamer caps 探测。
// 绝不输出凭据；凭据仅用于进程内构造 rtspsrc 属性。

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "railhmi/media/video_capability.h"

#include <gst/gst.h>

#include <im2d.h>  // RGA_API_FULL_VERSION（经 im2d_version.h）

#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace railhmi {
namespace media {

namespace {

long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const std::size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    const std::size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::string first_line(const std::string& s) {
    const std::size_t e = s.find('\n');
    return trim(e == std::string::npos ? s : s.substr(0, e));
}

// 把 secret 出现处替换为 ***；用于错误消息（可能回显 pipeline 串）。
std::string scrub_secret(std::string s, const std::string& secret) {
    if (secret.empty()) return s;
    std::size_t pos = 0;
    while ((pos = s.find(secret, pos)) != std::string::npos) {
        s.replace(pos, secret.size(), "***");
        pos += 3;
    }
    return s;
}

// 受限子进程执行（有界超时 + 有界输出）。返回 false 表示超时。
bool run_capture(const std::vector<std::string>& argv, int timeout_sec,
                 std::string* out, int* exit_code) {
    if (out) out->clear();
    if (exit_code) *exit_code = -1;
    if (argv.empty()) return false;

    int fds[2];
    if (::pipe(fds) != 0) return false;
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        return false;
    }
    if (pid == 0) {
        ::dup2(fds[1], STDOUT_FILENO);
        ::dup2(fds[1], STDERR_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        std::vector<char*> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);
        ::execvp(cargv[0], cargv.data());
        ::_exit(127);
    }
    ::close(fds[1]);
    const int flags = ::fcntl(fds[0], F_GETFL, 0);
    ::fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);

    const std::size_t kMaxOut = 64u * 1024u;
    const long long deadline = now_ms() + static_cast<long long>(timeout_sec) * 1000;
    bool timed_out = false;
    for (;;) {
        char buf[4096];
        const ssize_t n = ::read(fds[0], buf, sizeof(buf));
        if (n > 0) {
            if (out && out->size() < kMaxOut) {
                const std::size_t room = kMaxOut - out->size();
                out->append(buf, std::min<std::size_t>(static_cast<std::size_t>(n), room));
            }
            continue;
        }
        if (n == 0) break;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (now_ms() >= deadline) {
                timed_out = true;
                break;
            }
            ::usleep(20 * 1000);
            continue;
        }
        break;
    }
    if (timed_out) ::kill(pid, SIGKILL);
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        if (exit_code) *exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        if (exit_code) *exit_code = 128 + WTERMSIG(status);
    }
    return !timed_out;
}

// 读取文件并在 marker 之后取一段（用于共享库内嵌版本串）。有界读取。
std::string read_marker_after(const std::string& path, const std::string& marker) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    in.seekg(0, std::ios::end);
    std::streamoff n = in.tellg();
    in.seekg(0, std::ios::beg);
    if (n <= 0) return "";
    const std::streamoff kCap = 64LL << 20;
    const std::size_t want = static_cast<std::size_t>(std::min<std::streamoff>(n, kCap));
    std::string data;
    data.resize(want);
    in.read(&data[0], static_cast<std::streamsize>(want));
    data.resize(static_cast<std::size_t>(in.gcount()));
    const std::size_t p = data.find(marker);
    if (p == std::string::npos) return "";
    const std::size_t start = p + marker.size();
    std::size_t end = start;
    while (end < data.size() && end - start < 96) {
        const unsigned char c = static_cast<unsigned char>(data[end]);
        if (c < 0x20 || c > 0x7e) break;  // stop at NUL/newline/other binary noise
        ++end;
    }
    return trim(data.substr(start, end - start));
}

std::string rknnrt_version_from_lib() {
    void* h = ::dlopen("librknnrt.so", RTLD_LAZY | RTLD_LOCAL);
    if (!h) return "unknown(librknnrt.so not loadable)";
    std::string path;
    void* sym = ::dlsym(h, "rknn_init");
    Dl_info info;
    if (sym && ::dladdr(sym, &info) != 0 && info.dli_fname) path = info.dli_fname;
    ::dlclose(h);
    if (path.empty()) return "unknown(cannot resolve librknnrt path)";
    const std::string v = read_marker_after(path, "librknnrt version:");
    if (v.empty()) return "unknown(version marker not found)";
    return v;
}

std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x", static_cast<unsigned char>(c));
                    o += b;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

const char* position_str(core::CameraPosition p) { return core::to_string(p); }

}  // namespace

const char* to_string(CredentialStatus s) { return s == CredentialStatus::Present ? "present" : "missing"; }
const char* to_string(ElementStatus s) { return s == ElementStatus::Present ? "present" : "absent"; }

// ---------------------------------------------------------------------------
// 纯逻辑辅助
// ---------------------------------------------------------------------------
std::vector<std::string> required_gstreamer_elements() {
    return {"rtspsrc",   "rtph265depay", "h265parse", "rtph264depay", "h264parse",
            "decodebin", "mppvideodec",  "tee",       "queue",       "appsink",
            "fakesink"};
}

bool parse_gst_inspect(const std::string& element, const std::string& output,
                       int exit_code, ElementProbeResult* out) {
    if (!out) return false;
    out->element = element;
    out->status = ElementStatus::Absent;
    out->long_name.clear();
    out->detail.clear();

    if (exit_code == 0) {
        const std::size_t fd = output.find("Factory Details:");
        if (fd != std::string::npos) {
            out->status = ElementStatus::Present;
            const std::size_t ln = output.find("Long-name", fd);
            if (ln != std::string::npos) {
                const std::size_t eol = output.find('\n', ln);
                const std::string seg =
                    output.substr(ln + 9, (eol == std::string::npos ? output.size() : eol) - (ln + 9));
                out->long_name = trim(seg);
            }
            return true;
        }
        out->detail = "exit 0 but no Factory Details (unexpected gst-inspect output)";
        return false;
    }
    out->detail = first_line(output);
    if (out->detail.empty()) out->detail = "exit " + std::to_string(exit_code);
    return false;
}

CredentialStatus resolve_credential_env(const std::string& env_name,
                                        bool (*env_lookup)(const std::string&)) {
    if (env_name.empty() || !env_lookup) return CredentialStatus::Missing;
    return env_lookup(env_name) ? CredentialStatus::Present : CredentialStatus::Missing;
}

std::string evaluate_readiness(CredentialStatus credential,
                               const std::vector<std::string>& missing_elements) {
    if (credential == CredentialStatus::Missing) return "blocked_credential_missing";
    if (!missing_elements.empty()) return "blocked_element_missing:" + missing_elements.front();
    return "ready";
}

// ---------------------------------------------------------------------------
// 目标执行
// ---------------------------------------------------------------------------
ElementProbeResult probe_gstreamer_element(const std::string& element, int timeout_sec) {
    std::string out;
    int rc = -1;
    const bool done = run_capture({"gst-inspect-1.0", element}, timeout_sec, &out, &rc);
    ElementProbeResult r;
    if (!done) {
        r.element = element;
        r.status = ElementStatus::Absent;
        r.detail = "gst-inspect timeout after " + std::to_string(timeout_sec) + "s";
        return r;
    }
    parse_gst_inspect(element, out, rc, &r);
    if (r.detail.size() > 300) r.detail = r.detail.substr(0, 300);
    return r;
}

LibraryVersions probe_library_versions() {
    LibraryVersions v;
    guint major = 0, minor = 0, micro = 0, nano = 0;
    gst_version(&major, &minor, &micro, &nano);
    std::ostringstream g;
    g << major << "." << minor << "." << micro;
    v.gstreamer = g.str();
    v.librga = RGA_API_FULL_VERSION;
    v.librknnrt = rknnrt_version_from_lib();
    return v;
}

CapsRecord probe_camera_caps(const core::CameraConfig& cam, int timeout_ms) {
    CapsRecord rec;
    gst_init(nullptr, nullptr);  // 允许在无其它 GStreamer 调用者时独立探测
    const char* pw = std::getenv(cam.credential_env.c_str());
    if (!pw || !*pw) {
        rec.status = "error";
        rec.detail = "CREDENTIAL_MISSING";
        return rec;
    }
    const std::string secret(pw);

    // 凭据仅用于进程内构造 rtspsrc 属性；URL 本身不含凭据。
    std::ostringstream desc;
    desc << "rtspsrc name=src location=\"" << cam.rtsp_url << "\""
         << " user-id=\"" << cam.username << "\""
         << " user-pw=\"" << secret << "\""
         << " latency=200 timeout=5000000 protocols=udp"
         << " ! decodebin name=dec";

    std::string pipeline_desc = desc.str();
    GError* gerr = nullptr;
    GstElement* pipe = gst_parse_launch(pipeline_desc.c_str(), &gerr);
    // 立即擦除含凭据的 pipeline 串（日志安全）。
    std::fill(pipeline_desc.begin(), pipeline_desc.end(), '\0');
    pipeline_desc.clear();

    if (gerr) {
        rec.status = "error";
        rec.detail = scrub_secret(std::string("parse: ") + gerr->message, secret).substr(0, 300);
        g_error_free(gerr);
        if (pipe) gst_object_unref(pipe);
        return rec;
    }
    if (!pipe) {
        rec.status = "error";
        rec.detail = "pipeline build failed";
        return rec;
    }

    GstElement* dec = gst_bin_get_by_name(GST_BIN(pipe), "dec");
    if (dec) {
        // 只解码视频：避免音频 pad 干扰 delayed linking，并让 decodebin 只暴露 video/x-raw。
        GstCaps* vcaps = gst_caps_from_string("video/x-raw");
        g_object_set(dec, "caps", vcaps, NULL);
        gst_caps_unref(vcaps);
    }
    // 手动添加两个 fakesink：video/x-raw 进 video sink，其余（音频）进 drain sink，
    // 避免未链接 pad 触发 "Internal data stream error"。
    GstElement* sink = gst_element_factory_make("fakesink", "sink");
    GstElement* audio_sink = gst_element_factory_make("fakesink", "asink");
    if (sink) {
        g_object_set(sink, "sync", FALSE, NULL);
        gst_bin_add(GST_BIN(pipe), sink);  // bin 接管 floating ref
    }
    if (audio_sink) {
        g_object_set(audio_sink, "sync", FALSE, NULL);
        gst_bin_add(GST_BIN(pipe), audio_sink);
    }
    GstBus* bus = gst_element_get_bus(pipe);

    struct Trace { std::vector<std::string> lines; } trace;
    auto note_line = [&trace](const std::string& s) {
        if (trace.lines.size() < 12) trace.lines.push_back(s);
    };
    struct LinkCtx {
        GstElement* sink = nullptr;
        GstElement* audio_sink = nullptr;
        std::vector<std::string>* lines = nullptr;
        bool linked = false;
    } ctx;
    ctx.sink = sink;
    ctx.audio_sink = audio_sink;
    ctx.lines = &trace.lines;
    auto on_pad_added = +[](GstElement*, GstPad* pad, gpointer user) {
        auto* c = static_cast<LinkCtx*>(user);
        GstCaps* caps = gst_pad_get_current_caps(pad);
        if (c->lines && c->lines->size() < 12) {
            gchar* s = caps ? gst_caps_to_string(caps) : nullptr;
            std::string line = std::string("pad ") + GST_PAD_NAME(pad);
            if (s) {
                line += " caps=" + std::string(s).substr(0, 120);
                g_free(s);
            }
            c->lines->push_back(line);
        }
        const gchar* name = caps ? gst_structure_get_name(gst_caps_get_structure(caps, 0)) : nullptr;
        const bool is_video = name && g_str_has_prefix(name, "video/x-raw");
        if (caps) gst_caps_unref(caps);
        GstElement* target = is_video ? c->sink : c->audio_sink;
        if (!target) return;
        if (is_video && c->linked) return;
        GstPad* sp = gst_element_get_static_pad(target, "sink");
        if (sp && gst_pad_link(pad, sp) == GST_PAD_LINK_OK && is_video) c->linked = true;
        if (sp) gst_object_unref(sp);
    };
    if (dec) g_signal_connect(dec, "pad-added", G_CALLBACK(on_pad_added), &ctx);

    // 追踪 rtspsrc 的 RTP pad（只记录 caps，用于判断是否收到视频流）。
    auto on_rtp_pad = +[](GstElement*, GstPad* pad, gpointer user) {
        auto* lines = static_cast<std::vector<std::string>*>(user);
        if (lines->size() >= 12) return;
        GstCaps* caps = gst_pad_get_current_caps(pad);
        gchar* s = caps ? gst_caps_to_string(caps) : nullptr;
        std::string line = std::string("rtp ") + GST_PAD_NAME(pad);
        if (s) {
            line += " caps=" + std::string(s).substr(0, 120);
            g_free(s);
        }
        if (caps) gst_caps_unref(caps);
        lines->push_back(line);
    };
    GstElement* src = gst_bin_get_by_name(GST_BIN(pipe), "src");
    if (src) g_signal_connect(src, "pad-added", G_CALLBACK(on_rtp_pad), &trace.lines);

    // live 源（rtspsrc）不会 preroll：get_state 返回 NO_PREROLL。进入 PLAYING，
    // 轮询已协商 caps，直到有界超时。
    gst_element_set_state(pipe, GST_STATE_PAUSED);
    GstState cur = GST_STATE_VOID_PENDING, pending = GST_STATE_VOID_PENDING;
    const GstStateChangeReturn sr = gst_element_get_state(
        pipe, &cur, &pending,
        static_cast<GstClockTime>(std::min<long long>(3000, std::max(1000, timeout_ms))) *
            GST_MSECOND);
    gst_element_set_state(pipe, GST_STATE_PLAYING);

    bool errored = false;
    bool got_video = false;
    const long long deadline = now_ms() + std::max(1000, timeout_ms);
    while (!errored && !got_video && now_ms() < deadline) {
        if (bus) {
            GstMessage* msg = nullptr;
            while ((msg = gst_bus_pop_filtered(
                        bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS |
                                                         GST_MESSAGE_WARNING))) != nullptr) {
                const GstMessageType mt = GST_MESSAGE_TYPE(msg);
                if (mt == GST_MESSAGE_ERROR) {
                    GError* e = nullptr;
                    gchar* dbg = nullptr;
                    gst_message_parse_error(msg, &e, &dbg);
                    rec.status = "error";
                    rec.detail = scrub_secret(e ? e->message : "bus error", secret).substr(0, 300);
                    if (e) g_error_free(e);
                    g_free(dbg);
                    errored = true;
                } else if (mt == GST_MESSAGE_EOS) {
                    if (!errored) {
                        rec.status = "error";
                        rec.detail = "EOS before caps";
                        errored = true;
                    }
                } else if (mt == GST_MESSAGE_WARNING) {
                    GError* e = nullptr;
                    gchar* dbg = nullptr;
                    gst_message_parse_warning(msg, &e, &dbg);
                    note_line(scrub_secret(std::string("warn: ") + (e ? e->message : ""), secret));
                    if (e) g_error_free(e);
                    g_free(dbg);
                }
                gst_message_unref(msg);
            }
        }
        if (errored) break;
        if (sink) {
            GstPad* sp = gst_element_get_static_pad(sink, "sink");
            GstCaps* caps = sp ? gst_pad_get_current_caps(sp) : nullptr;
            if (caps && gst_caps_get_size(caps) > 0) {
                const gchar* name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
                if (name && g_str_has_prefix(name, "video/x-raw")) got_video = true;
            }
            if (caps) gst_caps_unref(caps);
            if (sp) gst_object_unref(sp);
        }
        if (!got_video) ::usleep(100 * 1000);
    }

    if (got_video && !errored) {
        if (sink) {
            GstPad* sp = gst_element_get_static_pad(sink, "sink");
            GstCaps* caps = sp ? gst_pad_get_current_caps(sp) : nullptr;
            if (caps && gst_caps_get_size(caps) > 0) {
                GstStructure* st = gst_caps_get_structure(caps, 0);
                const gchar* fmt = gst_structure_get_string(st, "format");
                if (fmt) rec.format = fmt;
                gint w = 0, h = 0;
                if (gst_structure_get_int(st, "width", &w)) rec.width = w;
                if (gst_structure_get_int(st, "height", &h)) rec.height = h;
                gint num = 0, den = 1;
                if (gst_structure_get_fraction(st, "framerate", &num, &den)) {
                    rec.fps_num = num;
                    rec.fps_den = den;
                }
                const gchar* color = gst_structure_get_string(st, "colorimetry");
                if (color) rec.colorimetry = color;
            }
            if (caps) gst_caps_unref(caps);
            if (sp) gst_object_unref(sp);
        }
        if (dec) {
            GstPad* dp = gst_element_get_static_pad(dec, "sink");
            GstCaps* rtp = dp ? gst_pad_get_current_caps(dp) : nullptr;
            if (rtp && gst_caps_get_size(rtp) > 0) {
                const gchar* enc = gst_structure_get_string(gst_caps_get_structure(rtp, 0), "encoding-name");
                if (enc) rec.codec = enc;
            }
            if (rtp) gst_caps_unref(rtp);
            if (dp) gst_object_unref(dp);
        }
        rec.status = "measured";
    } else if (!errored) {
        rec.status = "timeout";
        std::ostringstream d;
        d << "no negotiated caps within " << timeout_ms << "ms; state=" << static_cast<int>(cur)
          << " pending=" << static_cast<int>(pending) << " ret=" << static_cast<int>(sr);
        for (const auto& l : trace.lines) d << " | " << l;
        rec.detail = d.str().substr(0, 400);
    } else if (!trace.lines.empty()) {
        for (const auto& l : trace.lines) rec.detail += " | " + l;
        rec.detail = rec.detail.substr(0, 400);
    }

    gst_element_set_state(pipe, GST_STATE_NULL);
    if (bus) gst_object_unref(bus);
    if (src) gst_object_unref(src);
    if (dec) gst_object_unref(dec);
    gst_object_unref(pipe);  // bin 拥有 sink/audio_sink，随管线和自身释放
    return rec;
}

CapabilityReport build_capability_report(const core::AppSettings& settings,
                                         bool with_caps_probe, int caps_timeout_ms) {
    CapabilityReport r;
    r.config_ok = true;
    r.applied_defaults = settings.applied_defaults;
    r.render_path = core::to_string(settings.app.render_path);
    r.recording_source = core::to_string(settings.app.recording_source);

    r.versions = probe_library_versions();

    for (const auto& el : required_gstreamer_elements()) {
        r.elements.push_back(probe_gstreamer_element(el, 10));
    }
    for (const auto& e : r.elements) {
        if (e.status == ElementStatus::Absent) r.missing_required_elements.push_back(e.element);
    }
    r.required_elements_present = r.missing_required_elements.empty();

    bool probe_caps = with_caps_probe;

    for (const auto& cam : settings.cameras) {
        CameraCapability c;
        c.camera_id = cam.id;
        c.name = cam.name;
        c.position = position_str(cam.position);
        c.spectrum = core::to_string(cam.spectrum);
        c.ai_enabled = cam.ai_enabled;
        c.ai_profile = core::to_string(cam.ai_profile);
        c.display_fps = cam.display_fps;
        c.rtsp_url = cam.rtsp_url;
        c.credential_env = cam.credential_env;
        c.credential = resolve_credential_env(
            cam.credential_env, [](const std::string& n) -> bool {
                const char* v = std::getenv(n.c_str());
                return v && *v != '\0';
            });
        c.readiness = evaluate_readiness(c.credential, r.missing_required_elements);
        if (probe_caps && c.readiness == "ready") {
            c.caps = probe_camera_caps(cam, caps_timeout_ms);
            // 有界重试一次：仅针对网络/源瞬时错误（非凭据缺失/非不支持）。
            const bool transient =
                (c.caps.status == "error" || c.caps.status == "timeout") &&
                c.caps.detail.find("CREDENTIAL") == std::string::npos &&
                c.caps.detail.find("Unsupported") == std::string::npos;
            if (transient) {
                const std::string first = c.caps.status + ": " + c.caps.detail;
                ::usleep(2 * 1000 * 1000);
                CapsRecord retry = probe_camera_caps(cam, caps_timeout_ms);
                if (retry.status == "measured") {
                    c.caps = retry;
                } else {
                    c.caps = retry;
                    c.caps.detail = ("attempt1 [" + first + "] | attempt2 [" + retry.detail + "]")
                                        .substr(0, 400);
                }
            }
        } else {
            c.caps.status = "to_be_measured";
        }
        r.cameras.push_back(std::move(c));
        ::usleep(300 * 1000);  // 相机间短暂间隔，避免会话残留干扰
    }
    return r;
}

// ---------------------------------------------------------------------------
// 序列化
// ---------------------------------------------------------------------------
std::string capability_report_to_json(const CapabilityReport& r) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"config_ok\": " << (r.config_ok ? "true" : "false") << ",\n";
    o << "  \"render_path\": \"" << json_escape(r.render_path) << "\",\n";
    o << "  \"recording_source\": \"" << json_escape(r.recording_source) << "\",\n";
    o << "  \"versions\": {\"gstreamer\": \"" << json_escape(r.versions.gstreamer)
      << "\", \"librga\": \"" << json_escape(r.versions.librga) << "\", \"librknnrt\": \""
      << json_escape(r.versions.librknnrt) << "\"},\n";
    o << "  \"required_elements_present\": " << (r.required_elements_present ? "true" : "false") << ",\n";
    o << "  \"missing_required_elements\": [";
    for (std::size_t i = 0; i < r.missing_required_elements.size(); ++i)
        o << (i ? ", " : "") << "\"" << json_escape(r.missing_required_elements[i]) << "\"";
    o << "],\n";
    o << "  \"elements\": [\n";
    for (std::size_t i = 0; i < r.elements.size(); ++i) {
        const auto& e = r.elements[i];
        o << "    {\"element\": \"" << json_escape(e.element) << "\", \"status\": \""
          << to_string(e.status) << "\", \"long_name\": \"" << json_escape(e.long_name)
          << "\", \"detail\": \"" << json_escape(e.detail) << "\"}";
        o << (i + 1 < r.elements.size() ? ",\n" : "\n");
    }
    o << "  ],\n";
    o << "  \"cameras\": [\n";
    for (std::size_t i = 0; i < r.cameras.size(); ++i) {
        const auto& c = r.cameras[i];
        o << "    {\"camera_id\": \"" << json_escape(c.camera_id) << "\", \"name\": \""
          << json_escape(c.name) << "\", \"position\": \"" << json_escape(c.position)
          << "\", \"spectrum\": \"" << json_escape(c.spectrum)
          << "\", \"ai_enabled\": " << (c.ai_enabled ? "true" : "false")
          << ", \"ai_profile\": \"" << json_escape(c.ai_profile) << "\", \"display_fps\": "
          << c.display_fps << ", \"rtsp_url\": \"" << json_escape(c.rtsp_url)
          << "\", \"credential_env\": \"" << json_escape(c.credential_env)
          << "\", \"credential\": \"" << to_string(c.credential) << "\", \"readiness\": \""
          << json_escape(c.readiness) << "\", \"caps\": {\"status\": \"" << json_escape(c.caps.status)
          << "\", \"codec\": \"" << json_escape(c.caps.codec) << "\", \"format\": \""
          << json_escape(c.caps.format) << "\", \"width\": " << c.caps.width
          << ", \"height\": " << c.caps.height << ", \"fps_num\": " << c.caps.fps_num
          << ", \"fps_den\": " << c.caps.fps_den << ", \"colorimetry\": \""
          << json_escape(c.caps.colorimetry) << "\", \"detail\": \"" << json_escape(c.caps.detail)
          << "\"}}";
        o << (i + 1 < r.cameras.size() ? ",\n" : "\n");
    }
    o << "  ]\n";
    o << "}\n";
    return o.str();
}

std::string capability_report_to_text(const CapabilityReport& r) {
    std::ostringstream o;
    o << "type=capability_report config_ok=" << (r.config_ok ? "true" : "false")
      << " render_path=" << r.render_path << " recording_source=" << r.recording_source << "\n";
    o << "versions gstreamer=" << r.versions.gstreamer
      << " librga=\"" << r.versions.librga << "\" librknnrt=\"" << r.versions.librknnrt << "\"\n";
    o << "required_elements_present=" << (r.required_elements_present ? "true" : "false");
    if (!r.missing_required_elements.empty()) {
        o << " missing=";
        for (std::size_t i = 0; i < r.missing_required_elements.size(); ++i)
            o << (i ? "," : "") << r.missing_required_elements[i];
    }
    o << "\n";
    for (const auto& e : r.elements) {
        o << "element " << e.element << " status=" << to_string(e.status);
        if (!e.long_name.empty()) o << " long_name=\"" << e.long_name << "\"";
        if (!e.detail.empty()) o << " detail=\"" << e.detail << "\"";
        o << "\n";
    }
    for (const auto& c : r.cameras) {
        o << "camera " << c.camera_id << " name=\"" << c.name << "\" position=" << c.position
          << " spectrum=" << c.spectrum << " ai_enabled=" << (c.ai_enabled ? "true" : "false")
          << " ai_profile=" << c.ai_profile << " display_fps=" << c.display_fps << "\n";
        o << "  rtsp_url=\"" << c.rtsp_url << "\" credential_env=" << c.credential_env
          << " credential=" << to_string(c.credential) << " readiness=" << c.readiness << "\n";
        o << "  caps status=" << c.caps.status;
        if (!c.caps.codec.empty()) o << " codec=" << c.caps.codec;
        if (!c.caps.format.empty()) o << " format=" << c.caps.format;
        if (c.caps.width || c.caps.height) o << " size=" << c.caps.width << "x" << c.caps.height;
        if (c.caps.fps_num) o << " fps=" << c.caps.fps_num << "/" << c.caps.fps_den;
        if (!c.caps.colorimetry.empty()) o << " colorimetry=" << c.caps.colorimetry;
        if (!c.caps.detail.empty()) o << " detail=\"" << c.caps.detail << "\"";
        o << "\n";
    }
    return o.str();
}

}  // namespace media
}  // namespace railhmi
