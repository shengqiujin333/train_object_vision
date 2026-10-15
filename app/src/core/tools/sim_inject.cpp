// railhmi 模拟输入 / 故障注入 CLI（ITEM-009）
// 无需相机即可在模型层注入 FI-01..FI-05、FI-10 并驱动全部状态；输出结构化日志与 JSON。
// 不依赖 Qt/GStreamer/相机；可在目标板/最小工具链直接运行。
//
// 用法:
//   railhmi-sim-inject --fi 01,02 --duration 6000 --out sim.log --json sim.json
//   railhmi-sim-inject --list
//
// 退出码: 0 正常；2 参数错误。

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/core/simulation.h"

namespace {

std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\t': o += "\\t"; break;
            default: o += c;
        }
    }
    return o;
}

bool parse_faults(const std::string& csv, std::vector<railhmi::core::FaultKind>* out) {
    std::string cur;
    auto flush = [&]() -> bool {
        if (cur.empty()) return true;
        railhmi::core::FaultKind k;
        if (!railhmi::core::parse_fault(cur, &k)) return false;
        out->push_back(k);
        cur.clear();
        return true;
    };
    for (char c : csv) {
        if (c == ',' || c == ' ') { if (!flush()) return false; }
        else cur += c;
    }
    return flush();
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi::core;
    SimulationConfig cfg;
    std::string fi, out_path, json_path;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string* dst) { if (i + 1 < argc) *dst = argv[++i]; };
        if (a == "--camera") next(&cfg.camera_id);
        else if (a == "--profile") { std::string p; next(&p); parse_ai_profile(p, &cfg.profile); }
        else if (a == "--duration") { std::string v; next(&v); cfg.duration_ms = std::atoll(v.c_str()); }
        else if (a == "--step") { std::string v; next(&v); cfg.step_ms = std::atoll(v.c_str()); }
        else if (a == "--onset") { std::string v; next(&v); cfg.fault_onset_ms = std::atoll(v.c_str()); }
        else if (a == "--recovery") { std::string v; next(&v); cfg.fault_recovery_ms = std::atoll(v.c_str()); }
        else if (a == "--fi") next(&fi);
        else if (a == "--out") next(&out_path);
        else if (a == "--json") next(&json_path);
        else if (a == "--no-detections") cfg.feed_detections = false;
        else if (a == "--list") {
            std::printf("FI-01 stream_cut\nFI-02 freeze\nFI-03 ai_exit\nFI-04 disk_low\n"
                        "FI-05 ptz_out_of_tolerance\nFI-10 recording_interruption\n");
            return 0;
        } else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-sim-inject [--camera ID] [--profile p] [--duration ms] "
                        "[--step ms] [--onset ms] [--recovery ms] [--fi 01,02,...] "
                        "[--no-detections] [--out file] [--json file] [--list]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }

    if (!fi.empty() && !parse_faults(fi, &cfg.faults)) {
        std::fprintf(stderr, "invalid --fi list: %s\n", fi.c_str());
        return 2;
    }

    SimulationRunner runner(cfg);
    std::vector<Observation> obs = runner.run();

    std::ofstream out;
    if (!out_path.empty()) out.open(out_path, std::ios::trunc);
    for (const auto& o : obs) {
        const std::string line = format_observation(o);
        std::printf("%s\n", line.c_str());
        if (out) out << line << "\n";
    }

    if (!json_path.empty()) {
        std::ofstream j(json_path, std::ios::trunc);
        j << "[\n";
        for (std::size_t i = 0; i < obs.size(); ++i) {
            j << "  {\"t_ms\": " << obs[i].t_ms << ", \"type\": \"" << json_escape(obs[i].type)
              << "\", \"detail\": \"" << json_escape(obs[i].detail) << "\"}";
            j << (i + 1 < obs.size() ? ",\n" : "\n");
        }
        j << "]\n";
    }
    return 0;
}
