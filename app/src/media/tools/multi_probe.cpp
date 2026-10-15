// railhmi 六路并发取流探针（ITEM-015）
// 同时启动 N 路 CameraStream（默认 6），逐轮推进并输出每格状态（state/fps/最近帧/重连/presentable）
// 与网格汇总；可注入单路断流（FI-01）并程序化核对“断开一路后其余路继续刷新、该格保留且不重排”。
// 无相机时用 test 源；target 专用（GStreamer/librga）。
//
// 用法:
//   railhmi-multi-probe --count 6 --source test --duration 8 --cut-index 3 --cut-after 4 \
//     --step 500 --out m.log --json m.json
//
// 退出码: 0 正常（含验收判定行）；2 参数错误；3 启动失败（全部路启动失败）。

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/media/multi_stream.h"

namespace {

std::string fmt_fps(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

struct CellSnap {
    int idx = 0;
    std::string id;
    std::string state;
    double fps = 0.0;
    long long last_frame_ms = 0;
    int reconnect = 0;
    bool presentable = false;
};

struct Round {
    long long t_ms = 0;
    int live = 0;
    int presentable = 0;
    std::vector<CellSnap> cells;
};

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;
    int count = 6;
    std::string src = "test";
    long long duration_s = 8;
    long long cut_after_s = 4;
    int cut_index = 3;
    long long step_ms = 500;
    std::string out, json;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--count") { std::string v; next(&v); count = std::atoi(v.c_str()); }
        else if (a == "--source") next(&src);
        else if (a == "--duration") { std::string v; next(&v); duration_s = std::atoll(v.c_str()); }
        else if (a == "--cut-after") { std::string v; next(&v); cut_after_s = std::atoll(v.c_str()); }
        else if (a == "--cut-index") { std::string v; next(&v); cut_index = std::atoi(v.c_str()); }
        else if (a == "--step") { std::string v; next(&v); step_ms = std::atoll(v.c_str()); }
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-multi-probe --count 6 --source test|file|rtsp "
                        "[--duration s] [--cut-index i] [--cut-after s] [--step ms] [--out log] [--json json]\n");
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

    media::MultiStreamManager mgr(specs);
    std::string err;
    const int started = mgr.start_all(&err);
    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) fo << line << "\n";
    };
    emit("type=multi source=" + src + " channels=" + std::to_string(count) +
         " started=" + std::to_string(started) +
         " cut_index=" + std::to_string(cut_index) +
         " cut_after_s=" + std::to_string(cut_after_s));
    if (started == 0) {
        std::fprintf(stderr, "all channels failed to start: %s\n", err.c_str());
        return 3;
    }

    std::vector<Round> rounds;
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    long long prev_slot = -1;
    bool cut_done = false;
    long long cut_last_frame = -1;
    bool all_live_before = true;
    bool saw_before = false;

    while (true) {
        const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
        if (elapsed >= duration_s * 1000) break;
        if (cut_after_s >= 0 && !cut_done && elapsed >= cut_after_s * 1000) {
            mgr.simulate_cut(static_cast<std::size_t>(cut_index));
            cut_done = true;
            emit("t=" + std::to_string(elapsed) + " type=injection cut=FI-01 index=" + std::to_string(cut_index));
        }
        mgr.pump_all(120);
        const long long slot = elapsed / step_ms;
        if (slot == prev_slot) continue;
        prev_slot = slot;

        const std::vector<media::StreamStats> st = mgr.stats();
        Round r;
        r.t_ms = elapsed;
        for (std::size_t i = 0; i < st.size(); ++i) {
            CellSnap c;
            c.idx = static_cast<int>(i);
            c.id = mgr.ids()[i];
            c.state = railhmi::core::to_string(st[i].state);
            c.fps = st[i].fps;
            c.last_frame_ms = st[i].last_frame_ms;
            c.reconnect = st[i].reconnect_count;
            c.presentable = st[i].presentable;
            if (st[i].state == railhmi::core::StreamState::Live) ++r.live;
            if (st[i].presentable) ++r.presentable;
            r.cells.push_back(c);
            emit("t=" + std::to_string(elapsed) + " type=cell idx=" + std::to_string(c.idx) + " id=" + c.id +
                 " state=" + c.state + " fps=" + fmt_fps(c.fps) +
                 " last_frame_ms=" + std::to_string(c.last_frame_ms) +
                 " reconnect=" + std::to_string(c.reconnect) +
                 " presentable=" + std::string(c.presentable ? "true" : "false"));
        }
        emit("t=" + std::to_string(elapsed) + " type=grid cells=" + std::to_string(r.cells.size()) +
             " live=" + std::to_string(r.live) + " presentable=" + std::to_string(r.presentable));
        if (!cut_done) {
            saw_before = true;
            if (r.presentable != count) all_live_before = false;
        }
        if (cut_done && cut_last_frame < 0 && static_cast<std::size_t>(cut_index) < st.size()) {
            cut_last_frame = st[cut_index].last_frame_ms;
        }
        rounds.push_back(r);
    }

    // 断流后核对：cut 路不 presentable 且最近帧冻结；其余路仍 presentable。
    bool cut_not_presentable = false;
    bool others_still_live = true;
    bool cut_last_frame_frozen = true;
    for (const Round& r : rounds) {
        if (r.t_ms < cut_after_s * 1000) continue;
        if (static_cast<std::size_t>(cut_index) < r.cells.size()) {
            const CellSnap& c = r.cells[cut_index];
            if (!c.presentable) cut_not_presentable = true;
            if (cut_last_frame >= 0 && c.last_frame_ms != cut_last_frame) cut_last_frame_frozen = false;
        }
        for (const CellSnap& c : r.cells) {
            if (c.idx == cut_index) continue;
            if (!c.presentable) others_still_live = false;
        }
    }

    const std::vector<media::StreamStats> fin = mgr.stats();
    char live_after = 0, pres_after = 0;
    for (const auto& s : fin) { if (s.state == railhmi::core::StreamState::Live) ++live_after; if (s.presentable) ++pres_after; }
    emit("type=summary channels=" + std::to_string(count) +
         " final_live=" + std::to_string(static_cast<int>(live_after)) +
         " final_presentable=" + std::to_string(static_cast<int>(pres_after)));
    const bool ok = saw_before && all_live_before && cut_not_presentable && others_still_live && cut_last_frame_frozen;
    emit("type=acceptance all_presentable_before_cut=" + std::string(all_live_before ? "true" : "false") +
         " cut_not_presentable=" + std::string(cut_not_presentable ? "true" : "false") +
         " others_still_presentable=" + std::string(others_still_live ? "true" : "false") +
         " cut_last_frame_frozen=" + std::string(cut_last_frame_frozen ? "true" : "false") +
         " result=" + std::string(ok ? "PASS" : "FAIL"));
    mgr.stop_all();

    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        j << "{\n  \"channels\": " << count << ", \"cut_index\": " << cut_index
          << ", \"acceptance\": " << (ok ? "true" : "false") << ",\n  \"rounds\": [\n";
        for (std::size_t ri = 0; ri < rounds.size(); ++ri) {
            const Round& r = rounds[ri];
            j << "    {\"t_ms\": " << r.t_ms << ", \"live\": " << r.live
              << ", \"presentable\": " << r.presentable << ", \"cells\": [";
            for (std::size_t ci = 0; ci < r.cells.size(); ++ci) {
                const CellSnap& c = r.cells[ci];
                j << "{\"idx\": " << c.idx << ", \"id\": \"" << c.id << "\", \"state\": \"" << c.state
                  << "\", \"fps\": " << c.fps << ", \"last_frame_ms\": " << c.last_frame_ms
                  << ", \"reconnect\": " << c.reconnect
                  << ", \"presentable\": " << (c.presentable ? "true" : "false") << "}";
                if (ci + 1 < r.cells.size()) j << ", ";
            }
            j << "]}";
            j << (ri + 1 < rounds.size() ? ",\n" : "\n");
        }
        j << "  ]\n}\n";
    }
    return 0;
}
