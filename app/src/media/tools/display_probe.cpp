// railhmi-display (视频 ITEM-009)
// 验证显示分发与纹理交接：
//   * 向 GUI 提供不可变、拥有像素的显示快照（cpu 路径 BGR888）与 presentable；
//   * P1/P4/P5 视图共享同一相机来源；页面切换只改查询集合，不重建会话（generation 不变）；
//   * gl 路径为可开启候选，依赖（qmlglsink/glupload/glcolorconvert）缺失时显式不可用。
//
// 用法:
//   railhmi-display --source test --count 6 --out display.log --json display.json
//   railhmi-display --selftest
//
// 退出码: 0 正常（含验收判定行）；1 selftest 失败；2 参数错误；3 启动失败。

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "railhmi/media/display_registry.h"

namespace {

int run_selftest() {
    using namespace railhmi;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("selftest %s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    const media::RenderPathStatus cpu = media::probe_render_path(media::RenderPath::Cpu);
    check(cpu.available && cpu.reason.empty(), "cpu_path_available");
    const media::RenderPathStatus gl = media::probe_render_path(media::RenderPath::Gl);
    // gl 可可用（无 reason）或显式不可用（有 reason）；不得“可用但无说明”或“不可用且无说明”。
    check((gl.available && gl.reason.empty()) || (!gl.available && !gl.reason.empty()),
          "gl_status_explicit");
    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;
    std::string src = "test";
    int count = 6;
    std::string out, json;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--source") next(&src);
        else if (a == "--count") { std::string v; next(&v); count = std::atoi(v.c_str()); }
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--selftest") return run_selftest();
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-display --source test|file|rtsp [--count n] [--out log] "
                        "[--json json] [--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }

    std::vector<media::ChannelSpec> specs;
    for (int i = 0; i < count; ++i) {
        media::ChannelSpec sp;
        char id[16];
        std::snprintf(id, sizeof(id), "CAM-%02d", i + 1);
        sp.camera_id = id;
        sp.cfg.camera_id = id;
        sp.cfg.source = (src == "file") ? media::SourceKind::File
                       : (src == "rtsp") ? media::SourceKind::Rtsp
                                         : media::SourceKind::Test;
        specs.push_back(sp);
    }

    media::DisplayRegistry reg(specs, media::RenderPath::Cpu);
    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) fo << line << "\n";
    };

    const media::RenderPathStatus& cps = reg.render_path_status();
    emit("type=render_path path=cpu available=" + std::string(cps.available ? "true" : "false") +
         " reason=" + (cps.reason.empty() ? "-" : cps.reason));
    const media::RenderPathStatus gl = media::probe_render_path(media::RenderPath::Gl);
    emit("type=render_path path=gl available=" + std::string(gl.available ? "true" : "false") +
         " reason=" + (gl.reason.empty() ? "-" : gl.reason));

    std::string err;
    const int started = reg.start_all(&err);
    emit("type=multi source=" + src + " channels=" + std::to_string(count) +
         " started=" + std::to_string(started));
    if (started == 0) {
        std::fprintf(stderr, "all channels failed to start: %s\n", err.c_str());
        return 3;
    }

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    // 等首快照（有界）。
    media::FrameSnapshot first;
    bool got_first = false;
    for (int i = 0; i < 60 && !got_first; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        got_first = reg.snapshot("CAM-01", &first);
    }
    if (!got_first) {
        emit("type=summary first_snapshot=false");
        return 3;
    }

    std::vector<std::pair<std::string, unsigned long long>> gen0;
    for (const auto& id : reg.ids()) gen0.emplace_back(id, reg.generation(id));

    bool snapshots_valid = true;
    bool page_switch_no_rebuild = true;
    bool single_source = true;

    auto run_phase = [&](const std::string& phase, const std::vector<std::string>& ids, long long ms,
                         const std::string& extra) {
        const auto begin = clock::now();
        std::uint64_t updates_last = reg.updates(ids.empty() ? "CAM-01" : ids[0]);
        long long polls = 0;
        while (std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - begin).count() < ms) {
            for (const auto& id : ids) {
                media::FrameSnapshot a, b;
                const bool ok_a = reg.snapshot(id, &a);
                const bool ok_b = reg.snapshot(id, &b);
                if (ok_a) {
                    if (a.data.empty() || a.width <= 0 || a.height <= 0 ||
                        a.format != core::PixelFormat::Bgr888) {
                        snapshots_valid = false;
                    }
                    // 同一相机单一来源：两视图看到同一 camera_id 且 seq 单调不减。
                    if (!ok_b || b.camera_id != a.camera_id || b.seq < a.seq) single_source = false;
                }
                ++polls;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        // 页面切换不得重建会话：generation 与基线一致。
        for (const auto& g : gen0) {
            if (reg.generation(g.first) != g.second) page_switch_no_rebuild = false;
        }
        const std::string probe_id = ids.empty() ? "CAM-01" : ids[0];
        updates_last = reg.updates(probe_id);
        media::FrameSnapshot s;
        const bool sv = reg.snapshot(probe_id, &s);
        emit("type=phase phase=" + phase + " cameras=" + std::to_string(ids.size()) +
             " polls=" + std::to_string(polls) +
             " probe=" + probe_id +
             " snapshot_valid=" + std::string(sv ? "true" : "false") +
             " presentable=" + std::string(reg.presentable(probe_id) ? "true" : "false") +
             " seq=" + (sv ? std::to_string(s.seq) : "-") +
             " generation=" + std::to_string(reg.generation(probe_id)) +
             " updates=" + std::to_string(updates_last) +
             " format=" + (sv ? core::to_string(s.format) : "-") +
             " size=" + (sv ? std::to_string(s.width) + "x" + std::to_string(s.height) : "-") +
             " " + extra);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    };

    run_phase("P1", {"CAM-01", "CAM-02", "CAM-03"}, 1500, "view=left-front-right");
    run_phase("P4", reg.ids(), 1500, "view=all-cameras");
    run_phase("P5", {"CAM-01"}, 1200, "view=ai-debug");

    const bool gl_status_explicit = (gl.available && gl.reason.empty()) || (!gl.available && !gl.reason.empty());
    const bool ok = page_switch_no_rebuild && snapshots_valid && single_source && gl_status_explicit;
    emit("type=summary channels=" + std::to_string(count) +
         " page_switch_no_rebuild=" + std::string(page_switch_no_rebuild ? "true" : "false") +
         " snapshots_valid=" + std::string(snapshots_valid ? "true" : "false") +
         " single_source=" + std::string(single_source ? "true" : "false") +
         " gl_status_explicit=" + std::string(gl_status_explicit ? "true" : "false"));
    emit(std::string("type=acceptance result=") + (ok ? "PASS" : "FAIL"));
    reg.stop_all();

    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        j << "{\n  \"channels\": " << count
          << ", \"page_switch_no_rebuild\": " << (page_switch_no_rebuild ? "true" : "false")
          << ", \"snapshots_valid\": " << (snapshots_valid ? "true" : "false")
          << ", \"single_source\": " << (single_source ? "true" : "false")
          << ", \"gl_available\": " << (gl.available ? "true" : "false")
          << ", \"gl_reason\": \"" << gl.reason << "\""
          << ", \"acceptance\": " << (ok ? "true" : "false") << "\n}\n";
    }
    return 0;
}
