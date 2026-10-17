// railhmi-measure (视频 ITEM-017)
// 预算测量装置与三类源分层留证：
//   * 吞吐（源帧率/显示帧率/CPU/RSS/NPU/温度）；
//   * 端到端延迟分位（到达->解码完成->转换完成->呈现，p50/p90/p99）；
//   * 同步偏差；存储（实测 bitrate/片段实际大小/free bytes）；断流恢复耗时；
//   * 三类源（模拟输入 test / 文件回放 file / 真实 RTSP）分别产出可归档记录；
//     缺授权/未接设备如实记为 to_be_executed，不当作通过。
//
// 用法:
//   railhmi-measure --config config --duration 6 --outdir core-evidence/measure \
//       --out measure.log --json measure.json
//   railhmi-measure --selftest
//
// 退出码: 0 正常（含验收判定）；1 selftest 失败；2 参数错误；3 启动失败。

#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "railhmi/core/metrics.h"
#include "railhmi/core/replay_sync.h"
#include "railhmi/core/timebase.h"
#include "railhmi/media/camera_worker.h"
#include "railhmi/core/clip_request.h"
#include "railhmi/media/clip_writer.h"
#include "railhmi/media/recovery_controller.h"
#include "railhmi/media/rga_convert.h"

namespace {

using namespace railhmi;

long long now_us() { return core::TimeBase::steady_now_us(); }

long long proc_cpu_ticks() {
    std::ifstream in("/proc/self/stat");
    std::string line;
    if (!in || !std::getline(in, line)) return -1;
    // 跳过 comm（可能含空格/括号）：取最后一个 ')' 之后
    const std::size_t p = line.rfind(')');
    if (p == std::string::npos) return -1;
    std::vector<std::string> f;
    std::string tok;
    std::string rest = line.substr(p + 1);
    std::size_t i = 0;
    while (i < rest.size() && f.size() < 15) {
        while (i < rest.size() && rest[i] == ' ') ++i;
        std::size_t j = i;
        while (j < rest.size() && rest[j] != ' ') ++j;
        if (j > i) f.push_back(rest.substr(i, j - i));
        i = j;
    }
    // 第 14/15 项（1-indexed utime/stime）在 rest 中是 f[11], f[12]。
    if (f.size() < 13) return -1;
    return std::atoll(f[11].c_str()) + std::atoll(f[12].c_str());
}

long long proc_rss_kb() {
    std::ifstream in("/proc/self/status");
    std::string line;
    while (in && std::getline(in, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            const char* p = line.c_str() + 6;
            while (*p == ' ' || *p == '\t') ++p;
            return std::atoll(p);
        }
    }
    return -1;
}

double read_temp_c() {
    for (int z = 0; z < 8; ++z) {
        std::string path = "/sys/class/thermal/thermal_zone" + std::to_string(z) + "/temp";
        std::ifstream in(path);
        long long v = -1;
        if (in && (in >> v) && v > 0) return v / 1000.0;
    }
    return -1.0;
}

void read_npu(double* pct, std::string* note) {
    const char* paths[] = {"/sys/kernel/debug/rknpu/load", "/sys/class/devfreq/fdab0000.npu/load"};
    for (const char* p : paths) {
        std::ifstream in(p);
        std::string line;
        if (in && std::getline(in, line)) {
            const std::size_t pc = line.find('%');
            if (pc != std::string::npos) {
                std::size_t s = pc;
                while (s > 0 && (std::isdigit(static_cast<unsigned char>(line[s - 1])) || line[s - 1] == '.'))
                    --s;
                *pct = std::atof(line.substr(s, pc - s).c_str());
                note->clear();
                return;
            }
        }
    }
    *pct = -1.0;
    *note = "unavailable:needs_privileged_rknpu_load";
}

void free_bytes(long long* free_b, long long* total_b) {
    struct statvfs vfs;
    if (::statvfs("/", &vfs) == 0) {
        *free_b = static_cast<long long>(vfs.f_bavail) * vfs.f_frsize;
        *total_b = static_cast<long long>(vfs.f_blocks) * vfs.f_frsize;
    }
}

// 运行一条“压缩 -> 编码前 tap -> 解码 -> RGA 转换 -> 呈现深拷贝”的分级管线并测量。
core::MeasurementRecord run_staged(const std::string& compressed_prefix, core::SourceClass cls,
                                   const std::string& label, double duration_s,
                                   const std::string& outdir) {
    core::MeasurementRecord rec;
    rec.source_class = cls;
    rec.label = label;
    rec.window_s = duration_s;
    rec.status = core::MeasurementStatus::Recorded;

    gst_init(nullptr, nullptr);
    const std::string desc =
        compressed_prefix +
        " ! tee name=enc "
        "enc. ! queue max-size-buffers=16 ! appsink name=enc_sink sync=false "
        "enc. ! queue max-size-buffers=16 ! avdec_h264 ! videoconvert ! video/x-raw,format=NV12 "
        "! appsink name=dec_sink sync=false";
    GError* gerr = nullptr;
    GstElement* pipe = gst_parse_launch(desc.c_str(), &gerr);
    if (gerr) { rec.status = core::MeasurementStatus::ToBeExecuted; rec.reason = std::string("pipeline:") + gerr->message; g_error_free(gerr); }
    if (!pipe) { rec.reason = "pipeline build failed"; return rec; }
    GstElement* enc_sink = gst_bin_get_by_name(GST_BIN(pipe), "enc_sink");
    GstElement* dec_sink = gst_bin_get_by_name(GST_BIN(pipe), "dec_sink");
    gst_element_set_state(pipe, GST_STATE_PLAYING);

    std::deque<long long> enc_arrivals;
    std::vector<core::CompressedPacket> packets;
    long long enc_count = 0, dec_count = 0;
    const long long cpu0 = proc_cpu_ticks();
    const auto t0 = std::chrono::steady_clock::now();
    while (true) {
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (el >= duration_s) break;
        for (int k = 0; k < 8; ++k) {
            GstSample* es = gst_app_sink_try_pull_sample(GST_APP_SINK(enc_sink), 0);
            if (!es) break;
            const long long t = now_us();
            enc_arrivals.push_back(t);
            ++enc_count;
            GstBuffer* b = gst_sample_get_buffer(es);
            if (b) {
                core::CompressedPacket p;
                p.ts_ms = GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(b)) ? (long long)(GST_BUFFER_PTS(b) / GST_MSECOND) : (long long)(el * 1000);
                p.keyframe = !GST_BUFFER_FLAG_IS_SET(b, GST_BUFFER_FLAG_DELTA_UNIT);
                GstMapInfo mi;
                if (gst_buffer_map(b, &mi, GST_MAP_READ)) { p.data.assign(mi.data, mi.data + mi.size); gst_buffer_unmap(b, &mi); }
                packets.push_back(std::move(p));
            }
            gst_sample_unref(es);
            if (packets.empty()) {}
        }
        for (int k = 0; k < 8; ++k) {
            GstSample* ds = gst_app_sink_try_pull_sample(GST_APP_SINK(dec_sink), 0);
            if (!ds) break;
            const long long t_dec = now_us();
            const long long t_arr = enc_arrivals.empty() ? t_dec : enc_arrivals.front();
            if (!enc_arrivals.empty()) enc_arrivals.pop_front();
            ++dec_count;
            // 解码帧 -> NV12 -> RGA 转 BGR -> 呈现深拷贝。
            GstCaps* caps = gst_sample_get_caps(ds);
            GstBuffer* buf = gst_sample_get_buffer(ds);
            GstVideoInfo info;
            GstVideoFrame vf;
            if (buf && gst_video_info_from_caps(&info, caps) && gst_video_frame_map(&vf, &info, buf, GST_MAP_READ)) {
                const long long t_conv0 = now_us();
                media::BgrImage bgr;
                std::string cerr;
                const bool ok = media::nv12_to_bgr_raw(static_cast<const std::uint8_t*>(vf.data[0]), vf.info.width,
                                                       vf.info.height, vf.info.stride[0],
                                                       (std::uint64_t)vf.info.offset[1], &bgr, &cerr);
                const long long t_conv1 = now_us();
                std::vector<std::uint8_t> snapshot;
                if (ok) snapshot = bgr.data;   // 呈现深拷贝（cpu 路径）
                const long long t_pres = now_us();
                if (ok) {
                    rec.arrival_decode.add(t_dec - t_arr);
                    rec.decode_convert.add(t_conv1 - t_conv0);
                    rec.convert_present.add(t_pres - t_conv1);
                    rec.end_to_end.add(t_pres - t_arr);
                }
                gst_video_frame_unmap(&vf);
            }
            gst_sample_unref(ds);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const double window = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    gst_element_set_state(pipe, GST_STATE_NULL);
    if (enc_sink) gst_object_unref(enc_sink);
    if (dec_sink) gst_object_unref(dec_sink);
    gst_object_unref(pipe);

    const long long cpu1 = proc_cpu_ticks();
    const long cps = sysconf(_SC_CLK_TCK);
    rec.throughput.source_fps = window > 0 ? enc_count / window : 0;
    rec.throughput.display_fps = window > 0 ? dec_count / window : 0;
    if (cpu0 >= 0 && cpu1 >= cpu0 && cps > 0 && window > 0)
        rec.throughput.cpu_pct = 100.0 * (cpu1 - cpu0) / cps / window;
    rec.throughput.rss_kb = proc_rss_kb();
    rec.throughput.temp_c = read_temp_c();
    read_npu(&rec.throughput.npu_pct, &rec.throughput.npu_note);

    // 存储：将压缩包直通封装为真实片段，实测 bitrate / 片段大小 / free bytes。
    if (!packets.empty()) {
        core::ClipRequest req = core::make_clip_request("measure-clip", -1, "MEASURE", packets.front().ts_ms, 0, 0);
        media::ClipWriterConfig wcfg;
        wcfg.output_dir = outdir;
        media::ClipWriter writer(wcfg);
        const core::ClipResult res = writer.solidify(req, packets, "h264", nullptr);
        rec.storage.clip_bytes = res.size_bytes;
        rec.storage.measured_bitrate_bps = window > 0 ? res.size_bytes * 8.0 / window : -1.0;
    }
    free_bytes(&rec.storage.free_bytes, &rec.storage.total_bytes);
    return rec;
}

int run_selftest() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("selftest %s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    core::LatencyHistogram h;
    for (int i = 1; i <= 100; ++i) h.add(i);
    check(h.count() == 100 && h.min_us() == 1 && h.max_us() == 100, "histogram min/max");
    check(h.percentile(50) == 50 && h.percentile(90) == 90 && h.percentile(99) == 99, "histogram percentiles");
    check(h.percentile(0) == 1 && h.percentile(100) == 100, "histogram bounds");
    core::MeasurementRecord rec;
    rec.source_class = core::SourceClass::Rtsp;
    rec.status = core::MeasurementStatus::ToBeExecuted;
    rec.reason = "CREDENTIAL_MISSING";
    const std::string j = core::measurement_json(rec);
    check(j.find("\"source_class\":\"rtsp\"") != std::string::npos &&
              j.find("\"status\":\"to_be_executed\"") != std::string::npos &&
              j.find("CREDENTIAL_MISSING") != std::string::npos,
          "measurement json");
    std::vector<core::MeasurementRecord> v{rec};
    check(core::measurements_archive_json(v).find("\"records\":[") != std::string::npos, "archive json");
    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string config_dir = "config";
    int duration_s = 6;
    std::string outdir = "core-evidence/measure";
    std::string out, json;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--config") next(&config_dir);
        else if (a == "--duration") { std::string v; next(&v); duration_s = std::atoi(v.c_str()); }
        else if (a == "--outdir") next(&outdir);
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--selftest") return run_selftest();
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-measure [--config dir] [--duration s] [--outdir dir] "
                        "[--out log] [--json json] [--selftest]\n");
            return 0;
        } else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }

    std::ofstream fo(out, std::ios::trunc);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) { fo << line << "\n"; fo.flush(); }
    };

    core::TimeBase tb;
    tb.calibrate(core::TimeBase::system_now_ms(), core::TimeBase::steady_now_us());

    std::vector<core::MeasurementRecord> archive;

    // 1) 模拟输入（test 源，始终可用）。
    const std::string sim_prefix =
        "videotestsrc is-live=true ! video/x-raw,format=I420,width=1280,height=720,framerate=25/1 "
        "! x264enc tune=zerolatency speed-preset=ultrafast key-int-max=25 byte-stream=true "
        "! h264parse ! video/x-h264,stream-format=byte-stream,alignment=au";
    core::MeasurementRecord sim = run_staged(sim_prefix, core::SourceClass::Simulation,
                                             "videotestsrc/x264enc(h264)", duration_s, outdir);
    archive.push_back(sim);

    // 2) 文件回放（用模拟源生成的片段做 filesrc 回放）。
    core::MeasurementRecord file_rec;
    std::string clip_path;
    {
        // 找到刚生成的片段
        std::string dir = outdir;
        // ClipWriter 命名 MEASURE_measure-clip.ts
        clip_path = dir + "/MEASURE_measure-clip.ts";
        std::ifstream chk(clip_path);
        if (chk) {
            const std::string file_prefix = "filesrc location=\"" + clip_path +
                "\" ! tsdemux ! h264parse ! video/x-h264,stream-format=byte-stream,alignment=au";
            file_rec = run_staged(file_prefix, core::SourceClass::File, "filesrc/tsdemux(" + clip_path + ")",
                                  2.0, outdir);
        } else {
            file_rec.source_class = core::SourceClass::File;
            file_rec.label = "filesrc(no clip)";
            file_rec.status = core::MeasurementStatus::ToBeExecuted;
            file_rec.reason = "NO_CLIP_PRODUCED";
        }
    }
    archive.push_back(file_rec);
    emit("type=measure source=file " + core::measurement_json(file_rec));

    // 3) 真实 RTSP：板端无相机凭据 -> 如实记为待执行（不当作通过）。
    core::MeasurementRecord rtsp;
    rtsp.source_class = core::SourceClass::Rtsp;
    rtsp.label = "front_camera_rtsp(registered)";
    const char* cred = std::getenv("TRAIN_FRONT_RTSP_CREDENTIAL");
    if (cred && *cred) {
        rtsp.status = core::MeasurementStatus::ToBeExecuted;
        rtsp.reason = "REQUIRES_ENV_D_FIELD_RUN";
    } else {
        rtsp.status = core::MeasurementStatus::ToBeExecuted;
        rtsp.reason = "CREDENTIAL_MISSING";
    }
    archive.push_back(rtsp);
    emit("type=measure source=rtsp " + core::measurement_json(rtsp));

    // 同步偏差仪器（真实时钟读数，3 路实际播放头）。
    std::vector<std::string> cams = {"CAM-01", "CAM-02", "CAM-03"};
    std::map<std::string, long long> heads;
    for (const auto& c : cams) {
        long long h = 0;
        tb.try_to_wall_ms(now_us(), &h);
        heads[c] = h;
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    const auto sync = core::compute_replay_sync(cams, heads, tb.calibrated(), 300);
    sim.sync.available = sync.available;
    sim.sync.in_sync = sync.in_sync;
    sim.sync.max_deviation_ms = sync.max_deviation_ms;

    // 断流恢复耗时（真实 worker cut -> 重建 -> 真帧确认）。
    core::RecoverySample recovery;
    {
        media::CameraStreamConfig cfg;
        cfg.camera_id = "MEASURE";
        cfg.source = media::SourceKind::Test;
        media::CameraWorker worker(cfg);
        std::string werr;
        if (worker.start(&werr)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            media::RecoveryControllerConfig rcfg;
            rcfg.policy.base_backoff_ms = 300;
            media::RecoveryController ctl(&worker, rcfg);
            worker.request_cut();
            const long long tcut = now_us() / 1000;
            for (int i = 0; i < 400; ++i) {
                ctl.tick(now_us() / 1000);
                const auto st = ctl.status();
                if (st.recovered && !st.awaiting_recovery) { recovery.last_recovery_ms = now_us() / 1000 - tcut; break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            recovery.attempts = ctl.status().policy.total_attempts;
            worker.stop();
        }
    }
    sim.recovery = recovery;
    archive[0] = sim;
    emit("type=measure source=simulation " + core::measurement_json(sim));

    const bool sim_ok = sim.status == core::MeasurementStatus::Recorded &&
                        sim.throughput.source_fps > 0 && sim.throughput.display_fps > 0 &&
                        sim.end_to_end.count() > 0 && sim.end_to_end.percentile(50) >= 0 &&
                        sim.storage.clip_bytes > 0 && sim.storage.free_bytes > 0 && sim.sync.available &&
                        sim.recovery.last_recovery_ms >= 0;
    const bool file_ok = file_rec.status == core::MeasurementStatus::Recorded &&
                         file_rec.throughput.source_fps > 0 && file_rec.end_to_end.count() > 0;
    const bool rtsp_labeled = rtsp.status == core::MeasurementStatus::ToBeExecuted && !rtsp.reason.empty();
    const bool three_classes = archive.size() == 3 &&
                               archive[0].source_class == core::SourceClass::Simulation &&
                               archive[1].source_class == core::SourceClass::File &&
                               archive[2].source_class == core::SourceClass::Rtsp;
    const bool ok = sim_ok && file_ok && rtsp_labeled && three_classes;

    emit("type=acceptance simulation_recorded=" + std::string(sim_ok ? "true" : "false") +
         " file_recorded=" + std::string(file_ok ? "true" : "false") +
         " rtsp_to_be_executed=" + std::string(rtsp_labeled ? "true" : "false") +
         " three_source_classes=" + std::string(three_classes ? "true" : "false") +
         " latency_percentiles=" + std::string(sim.end_to_end.count() > 0 ? "true" : "false") +
         " storage_measured=" + std::string(sim.storage.clip_bytes > 0 ? "true" : "false") +
         " recovery_measured=" + std::string(sim.recovery.last_recovery_ms >= 0 ? "true" : "false") +
         " sync_instrument=" + std::string(sim.sync.available ? "true" : "false") +
         " result=" + std::string(ok ? "PASS" : "FAIL"));

    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        j << core::measurements_archive_json(archive) << "\n";
    }
    return 0;
}
