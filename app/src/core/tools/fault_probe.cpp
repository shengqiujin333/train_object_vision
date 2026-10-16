// railhmi 故障自检目录探针（ITEM-025）
// 打印 FI-01..FI-05/FI-10 的显式提示与恢复路径，以及板端自检清单（注入->期望提示->恢复路径）。
// 纯 core，无 Qt。
//
// 用法: railhmi-fault-probe [--out log]
// 退出码: 0 验收 PASS；1 FAIL。

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "railhmi/core/fault_catalog.h"

using namespace railhmi::core;

int main(int argc, char** argv) {
    std::string out;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--out") next(&out);
        else if (a == "--help" || a == "-h") { std::printf("usage: railhmi-fault-probe [--out log]\n"); return 0; }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }
    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) { std::printf("%s\n", line.c_str()); if (fo) fo << line << "\n"; };

    const std::vector<FaultPrompt> cat = fault_catalog();
    emit("type=fault-catalog count=" + std::to_string(cat.size()));
    bool all_nonempty = true;
    for (const auto& f : cat) {
        if (f.id.empty() || f.prompt.empty() || f.recovery.empty()) all_nonempty = false;
        emit("fault id=" + f.id + " label=" + f.label + " prompt=\"" + f.prompt + "\" recovery=\"" + f.recovery + "\"");
    }
    const std::vector<SelfCheckStep> sc = self_check_list();
    emit("type=self-check count=" + std::to_string(sc.size()));
    for (const auto& s : sc)
        emit("step id=" + s.id + " inject=\"" + s.inject + "\" prompt=\"" + s.expected_prompt +
             "\" recovery=\"" + s.recovery_path + "\"");

    // 必须覆盖 FI-01/02/03/04/05/10 与一次启动恢复自检
    auto has = [&](const std::string& id) {
        for (const auto& f : cat) if (f.id == id) return true;
        return false;
    };
    const bool covered = has("FI-01") && has("FI-02") && has("FI-03") && has("FI-04") && has("FI-05") && has("FI-10");
    bool has_boot = false;
    for (const auto& s : sc) if (s.id == "SC-BOOT") has_boot = true;
    const bool ok = covered && has_boot && all_nonempty && cat.size() == 6 && sc.size() >= 7;
    emit(std::string("type=acceptance fi_covered=") + (covered ? "true" : "false") +
         " boot_step=" + (has_boot ? "true" : "false") +
         " prompts_nonempty=" + (all_nonempty ? "true" : "false") +
         " result=" + (ok ? "PASS" : "FAIL"));
    return ok ? 0 : 1;
}
