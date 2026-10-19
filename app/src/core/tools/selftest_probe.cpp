// railhmi-vision-selftest（视觉推理 ITEM-013）
// 无相机自检入口：用确定性合成/夹具输入复现各后处理与可用性路径，并输出可归档结构化 JSON。
//
// 用法:
//   railhmi-vision-selftest --selftest [--json out.json] [--out log] [--deterministic] [--verbose]
//   railhmi-vision-selftest --list
//
// 退出码: 0 全部通过；1 有失败；2 参数错误。
//
// 边界：本工具不加载 RKNN、不连接相机；真机精度与人工视觉确认为 pending（见 JSON）。

#include <cstdio>
#include <fstream>
#include <string>

#include "railhmi/core/vision_selftest.h"

using namespace railhmi::core;

namespace {

std::ofstream g_fo;

void emit(const std::string& line) {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
    if (g_fo) {
        g_fo << line << "\n";
        g_fo.flush();
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string out;
    std::string json;
    bool selftest = false;
    bool deterministic = false;
    bool verbose = false;
    bool list = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) {
            if (i + 1 < argc) *d = argv[++i];
        };
        if (a == "--selftest") selftest = true;
        else if (a == "--json") next(&json);
        else if (a == "--out") next(&out);
        else if (a == "--deterministic") deterministic = true;
        else if (a == "--verbose") verbose = true;
        else if (a == "--list") list = true;
        else if (a == "--help" || a == "-h") {
            std::printf(
                "usage: railhmi-vision-selftest --selftest [--json out.json] [--out log] "
                "[--deterministic] [--verbose]\n"
                "       railhmi-vision-selftest --list\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }

    if (list) {
        for (const auto& id : selftest_case_ids()) std::printf("%s\n", id.c_str());
        return 0;
    }

    (void)selftest;
    g_fo.open(out, std::ios::trunc);
    const SelfTestReport r = run_vision_selftest();

    emit("type=selftest schema=" + r.schema + " no_device=" + (r.no_device ? "true" : "false") +
         " environment=" + r.environment + " cases=" + std::to_string(r.cases.size()));
    for (const auto& c : r.cases) {
        emit(std::string("[") + (c.pass ? "PASS" : "FAIL") + "] " + c.area + "/" + c.id +
             (c.detail.empty() ? "" : (" :: " + c.detail)) +
             (c.requirement.empty() ? "" : (" [" + c.requirement + "]")));
        if (verbose) {
            for (const auto& kv : c.evidence)
                emit("    evidence " + kv.first + "=" + kv.second);
        }
    }
    for (const auto& p : r.pending) emit("type=pending text=" + p);
    emit("type=summary total=" + std::to_string(r.cases.size()) +
         " passed=" + std::to_string(r.passed) + " failed=" + std::to_string(r.failed));
    emit(std::string("RESULT: ") + (r.failed == 0 ? "PASS" : "FAIL") +
         " failures=" + std::to_string(r.failed));

    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        if (!j) {
            std::fprintf(stderr, "cannot write --json: %s\n", json.c_str());
            return 2;
        }
        j << selftest_json(r, !deterministic) << "\n";
    }
    return r.failed == 0 ? 0 : 1;
}
