// railhmi 保护片段/缺口探针（ITEM-019）
// 演示：有界滚动缓冲、报警固化 [pre,post] 保护片段、post 后 finalize、
// 原子写盘（临时文件 -> 重命名，经 platform::FileStore）、保留策略保护片段优先、
// 录像中断写入并合并缺口记录。无 Qt/相机。
//
// 用法: railhmi-clip-probe [--dir DIR] [--out log]
// 退出码: 0 验收 PASS；1 FAIL；2 参数错误。

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/core/clip.h"
#include "railhmi/platform/file_store.h"

using namespace railhmi::core;

int main(int argc, char** argv) {
    std::string dir = "/tmp/railhmi_clips";
    std::string out;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--dir") next(&dir);
        else if (a == "--out") next(&out);
        else if (a == "--help" || a == "-h") { std::printf("usage: railhmi-clip-probe [--dir DIR] [--out log]\n"); return 0; }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }
    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) { std::printf("%s\n", line.c_str()); if (fo) fo << line << "\n"; };

    ClipConfig cfg;
    cfg.pre_seconds = 30;
    cfg.post_seconds = 60;
    RetentionPolicy pol;
    pol.retention_days = 7;
    pol.max_gb = 12.0;
    ClipRecorder rec(cfg, pol);

    emit("type=config pre_s=" + std::to_string(cfg.pre_seconds) + " post_s=" + std::to_string(cfg.post_seconds) +
         " buffer=" + std::to_string(rec.config().max_frames_per_channel) + " capacity_gb=12");

    for (long long t = 0; t <= 130000; t += 40) rec.on_frame("CAM-01", t);
    emit("type=buffer cam=CAM-01 frames=" + std::to_string(rec.buffer("CAM-01") ? rec.buffer("CAM-01")->size() : 0) +
         " capacity=" + std::to_string(rec.config().max_frames_per_channel));

    const long long trigger = 60000;
    const ProtectedClip c = rec.on_alarm("CAM-01", AlarmLevel::L1, trigger);
    emit("type=clip id=" + std::to_string(c.id) + " cam=" + c.cam + " trigger_ms=" + std::to_string(c.trigger_ms) +
         " t0_ms=" + std::to_string(c.t0_ms) + " t1_ms=" + std::to_string(c.t1_ms) +
         " duration_ms=" + std::to_string(c.duration_ms()) + " state=" + to_string(c.state));

    emit("type=finalize before=" + std::to_string(rec.finalize(119000)) +
         " at=" + std::to_string(rec.finalize(120000)));
    const ProtectedClip& fc = rec.clips()[0];
    emit("type=clip id=" + std::to_string(fc.id) + " state=" + to_string(fc.state) +
         " complete=" + (fc.complete ? "true" : "false") + " frames=" + std::to_string(fc.frame_count) +
         " duration_ms=" + std::to_string(fc.duration_ms()));

    // 原子写盘：临时文件 -> 重命名
    std::string err;
    const std::string path = dir + "/clip-" + std::to_string(fc.id) + ".json";
    const std::string json = clip_manifest_json(fc);
    const bool dir_ok = railhmi::platform::FileStore::ensure_parent_dir(path, &err);
    const bool wrote = railhmi::platform::FileStore::write_atomic(path, json, &err);
    const std::string back = railhmi::platform::FileStore::read_all(path, &err);
    emit("type=clip-store dir_ok=" + std::string(dir_ok ? "true" : "false") +
         " wrote=" + std::string(wrote ? "true" : "false") +
         " exists=" + std::string(railhmi::platform::FileStore::exists(path) ? "true" : "false") +
         " path=" + path);
    emit("type=clip-manifest " + back);

    // 保留策略：普通片段先淘汰，保护片段保留
    const long long g = 1024LL * 1024LL * 1024LL;
    rec.set_clip_size(fc.id, estimate_clip_size(fc, 4.0));   // 4 Mbps
    rec.add_normal_segment(101, "CAM-02", 0, 3600000, 8 * g);
    rec.add_normal_segment(102, "CAM-03", 0, 3600000, 8 * g);
    const RetentionResult r = rec.apply_retention_now(120000);
    emit("type=retention evicted=" + std::to_string(static_cast<int>(r.evicted_ids.size())) +
         " continuous=" + std::to_string(r.evicted_continuous) +
         " critical=" + std::to_string(r.evicted_critical) +
         " protected_remaining=" + std::to_string(rec.clips().size()));

    // 录像中断 -> 缺口记录（重叠合并）
    rec.on_recording_interrupt("CAM-01", 200000, 205000, "recording_interrupted");
    rec.on_recording_interrupt("CAM-01", 204000, 210000, "recording_interrupted");
    emit("type=gap cam=CAM-01 count=" + std::to_string(rec.gaps().size()) +
         " t0=" + std::to_string(rec.gaps().empty() ? 0 : rec.gaps()[0].t0_ms) +
         " t1=" + std::to_string(rec.gaps().empty() ? 0 : rec.gaps()[0].t1_ms) +
         " reason=" + (rec.gaps().empty() ? "" : rec.gaps()[0].reason));

    const bool ok = (fc.duration_ms() == 90000) && fc.complete && wrote && (back == json) &&
                    (r.evicted_continuous == 1) && (r.evicted_critical == 0) && (rec.clips().size() == 1) &&
                    (rec.gaps().size() == 1);
    emit(std::string("type=acceptance duration_ms=") + std::to_string(fc.duration_ms()) +
         " clip_complete=" + (fc.complete ? "true" : "false") +
         " clip_file_exists=" + (railhmi::platform::FileStore::exists(path) ? "true" : "false") +
         " protected_priority=" + (r.evicted_critical == 0 ? "true" : "false") +
         " gap_merged=" + (rec.gaps().size() == 1 ? "true" : "false") +
         " result=" + (ok ? "PASS" : "FAIL"));
    return ok ? 0 : 1;
}
