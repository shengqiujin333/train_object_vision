// railhmi 多路并发取流探针（视频 ITEM-007；取代旧 ITEM-015 顺序 pump 版）
// 每路独立 worker 线程并发推进；单路启动失败(FI)/断流(FI-01)不影响其余路；
// 槽位顺序不变、不重排、不丢格；pump 不在本（调用）线程执行。
//
// 用法:
//   railhmi-multi-probe --count 6 --source test --duration 8 --cut-index 3 --cut-after 4 \
//     --fail-index 5 --step 500 --out m.log --json m.json
//
// 退出码: 0 正常（含验收判定行）；2 参数错误；3 全部路启动失败。

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
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
    bool lease_valid = false;
    unsigned long long generation = 0;
};

struct Round {
    long long t_ms = 0;
    int live = 0;
    int presentable = 0;
    std::vector<CellSnap> cells;
};

// 管理器纯/轻量自检：槽位顺序不变、索引查找、单路启动失败隔离。
int run_selftest() {
    using namespace railhmi;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("selftest %s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    std::vector<media::ChannelSpec> specs;
    for (int i = 0; i < 6; ++i) {
        media::ChannelSpec sp;
        char id[16];
        std::snprintf(id, sizeof(id), "CAM-%02d", i + 1);
        sp.camera_id = id;
        sp.cfg.camera_id = id;
        sp.cfg.source = media::SourceKind::Test;
        sp.start_failure = (i == 5);
        specs.push_back(sp);
    }
    media::MultiStreamManager mgr(specs);
    check(mgr.size() == 6, "manager_size");
    check(mgr.ids()[0] == "CAM-01" && mgr.ids()[5] == "CAM-06", "slot_order_config");
    check(mgr.index_of("CAM-04") == 3 && mgr.index_of("nope") == -1, "index_of");
    std::string err;
    const int started = mgr.start_all(&err);
    check(started == 5, "start_all_isolates_single_failure");
    check(!mgr.started(5) && !mgr.start_error(5).empty(), "fail_channel_reported");
    check(mgr.ids().size() == 6 && mgr.ids()[5] == "CAM-06", "slot_order_after_start");
    mgr.stop_all();
    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;
    int count = 6;
    std::string src = "test";
    long long duration_s = 8;
    long long cut_after_s = 4;
    int cut_index = 3;
    int fail_index = -1;
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
        else if (a == "--fail-index") { std::string v; next(&v); fail_index = std::atoi(v.c_str()); }
        else if (a == "--step") { std::string v; next(&v); step_ms = std::atoll(v.c_str()); }
        else if (a == "--selftest") return run_selftest();
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-multi-probe --count 6 --source test|file|rtsp "
                        "[--duration s] [--cut-index i] [--cut-after s] [--fail-index i] "
                        "[--step ms] [--out log] [--json json]\n");
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
        sp.start_failure = (i == fail_index);
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
         " cut_after_s=" + std::to_string(cut_after_s) +
         " fail_index=" + std::to_string(fail_index));
    for (std::size_t i = 0; i < mgr.size(); ++i) {
        emit("type=start idx=" + std::to_string(i) + " id=" + mgr.ids()[i] +
             " started=" + std::string(mgr.started(i) ? "true" : "false") +
             " error=" + (mgr.start_error(i).empty() ? "-" : mgr.start_error(i)));
    }
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
    bool before_ok = true;
    bool saw_before = false;

    while (true) {
        const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
        if (elapsed >= duration_s * 1000) break;
        if (cut_after_s >= 0 && !cut_done && elapsed >= cut_after_s * 1000) {
            mgr.request_cut(static_cast<std::size_t>(cut_index));
            cut_done = true;
            emit("t=" + std::to_string(elapsed) + " type=injection cut=FI-01 index=" + std::to_string(cut_index));
        }
        // workers 自行推进取流；本线程只采样（不在本线程 pump）。
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        const long long slot = elapsed / step_ms;
        if (slot == prev_slot) continue;
        prev_slot = slot;

        const std::vector<media::StreamStats> st = mgr.stats();
        Round r;
        r.t_ms = elapsed;
        for (std::size_t i = 0; i < st.size(); ++i) {
            const bool pres = mgr.is_presentable(i);
            media::FrameLease l = mgr.latest_frame(i);
            CellSnap c;
            c.idx = static_cast<int>(i);
            c.id = mgr.ids()[i];
            c.state = railhmi::core::to_string(st[i].state);
            c.fps = st[i].fps;
            c.last_frame_ms = st[i].last_frame_ms;
            c.reconnect = st[i].reconnect_count;
            c.presentable = pres;
            c.lease_valid = l.valid();
            c.generation = st[i].generation;
            if (st[i].state == railhmi::core::StreamState::Live) ++r.live;
            if (pres) ++r.presentable;
            r.cells.push_back(c);
            emit("t=" + std::to_string(elapsed) + " type=cell idx=" + std::to_string(c.idx) + " id=" + c.id +
                 " state=" + c.state + " fps=" + fmt_fps(c.fps) +
                 " last_frame_ms=" + std::to_string(c.last_frame_ms) +
                 " reconnect=" + std::to_string(c.reconnect) +
                 " presentable=" + std::string(c.presentable ? "true" : "false") +
                 " lease=" + std::string(c.lease_valid ? "true" : "false") +
                 " generation=" + std::to_string(c.generation));
        }
        emit("t=" + std::to_string(elapsed) + " type=grid cells=" + std::to_string(r.cells.size()) +
             " live=" + std::to_string(r.live) + " presentable=" + std::to_string(r.presentable));
        if (!cut_done) {
            saw_before = true;
            // 断流前：除故障注入路外，其余路均应 presentable。
            for (const CellSnap& c : r.cells) {
                if (c.idx == fail_index) continue;
                if (!c.presentable) before_ok = false;
            }
        }
        if (cut_done && cut_last_frame < 0 && static_cast<std::size_t>(cut_index) < st.size()) {
            cut_last_frame = st[cut_index].last_frame_ms;
        }
        rounds.push_back(r);
    }

    // 断流后：cut 路不 presentable 且最近帧冻结；其余非故障路仍 presentable；故障路始终不 presentable。
    bool cut_not_presentable = false;
    bool others_still_live = true;
    bool cut_last_frame_frozen = true;
    bool fail_isolated = true;
    bool slot_order_stable = true;
    for (const Round& r : rounds) {
        if (r.cells.size() != static_cast<std::size_t>(count)) slot_order_stable = false;
        for (const CellSnap& c : r.cells) {
            if (c.idx == fail_index) {
                if (c.presentable || c.lease_valid) fail_isolated = false;
                continue;
            }
            if (c.idx == cut_index) {
                if (r.t_ms >= cut_after_s * 1000) {
                    if (!c.presentable) cut_not_presentable = true;
                    if (cut_last_frame >= 0 && c.last_frame_ms != cut_last_frame) cut_last_frame_frozen = false;
                }
                continue;
            }
            if (r.t_ms >= cut_after_s * 1000 && !c.presentable) others_still_live = false;
        }
    }
    const bool fail_has_error = (fail_index >= 0)
        ? !mgr.start_error(static_cast<std::size_t>(fail_index)).empty()
        : true;
    if (fail_index >= 0 && !fail_has_error) fail_isolated = false;

    const std::vector<media::StreamStats> fin = mgr.stats();
    int live_after = 0, pres_after = 0;
    for (const auto& s : fin) { if (s.state == railhmi::core::StreamState::Live) ++live_after; if (s.presentable) ++pres_after; }
    emit("type=summary channels=" + std::to_string(count) +
         " final_live=" + std::to_string(live_after) +
         " final_presentable=" + std::to_string(pres_after));
    const bool ok = saw_before && before_ok && slot_order_stable && cut_not_presentable &&
                    others_still_live && cut_last_frame_frozen && fail_isolated;
    emit("type=acceptance before_ok=" + std::string(before_ok ? "true" : "false") +
         " slot_order_stable=" + std::string(slot_order_stable ? "true" : "false") +
         " cut_not_presentable=" + std::string(cut_not_presentable ? "true" : "false") +
         " others_still_presentable=" + std::string(others_still_live ? "true" : "false") +
         " cut_last_frame_frozen=" + std::string(cut_last_frame_frozen ? "true" : "false") +
         " fail_isolated=" + std::string(fail_isolated ? "true" : "false") +
         " result=" + std::string(ok ? "PASS" : "FAIL"));
    mgr.stop_all();

    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        j << "{\n  \"channels\": " << count << ", \"cut_index\": " << cut_index
          << ", \"fail_index\": " << fail_index
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
                  << ", \"presentable\": " << (c.presentable ? "true" : "false")
                  << ", \"lease\": " << (c.lease_valid ? "true" : "false")
                  << ", \"generation\": " << c.generation << "}";
                if (ci + 1 < r.cells.size()) j << ", ";
            }
            j << "]}";
            j << (ri + 1 < rounds.size() ? ",\n" : "\n");
        }
        j << "  ]\n}\n";
    }
    return 0;
}
