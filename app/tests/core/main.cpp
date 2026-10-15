// railhmi-core 单元测试入口（ITEM-003 骨架；ITEM-004..008 扩展；ITEM-008 单命令可归档入口）。
// 仅依赖 C++ 标准库 + railhmi_core/railhmi_platform（SQLite 存在时启用持久化测试）；
// 无 Qt/GStreamer/相机依赖。运行命令：见 app/tests/run_core_tests.sh。

#include <cstdio>
#include <string>

#include "railhmi/core/version.h"
#include "test_util.h"

void run_platform_tests();
void run_config_tests();
void run_stream_tests();
void run_rule_tests();
void run_persistence_tests();
void run_simulation_tests();
void run_navigation_tests();
void run_registry_tests();
void run_layout_tests();
void run_grid_tests();
void run_status_tests();
void run_inference_tests();
void run_alarm_center_tests();
void run_clip_tests();
void run_replay_tests();

int main(int argc, char** argv) {
    std::string junit;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--junit" && i + 1 < argc) junit = argv[++i];
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-core-tests [--junit <path>]\n");
            return 0;
        }
    }

    std::printf("== railhmi-core tests (%s) ==\n", railhmi::core::build_description().c_str());
    run_platform_tests();
    run_config_tests();
    run_stream_tests();
    run_rule_tests();
    run_persistence_tests();
    run_simulation_tests();
    run_navigation_tests();
    run_registry_tests();
    run_layout_tests();
    run_grid_tests();
    run_status_tests();
    run_inference_tests();
    run_alarm_center_tests();
    run_clip_tests();
    run_replay_tests();

    const bool ok = railhmi_test::g_fail == 0;
    std::printf("== %d passed, %d failed ==\n", railhmi_test::g_pass, railhmi_test::g_fail);
    std::printf("RESULT: %s passed=%d failed=%d\n", ok ? "PASS" : "FAIL",
                railhmi_test::g_pass, railhmi_test::g_fail);

    if (!junit.empty()) {
        if (railhmi_test::write_junit(junit, "railhmi-core-tests"))
            std::printf("[junit] wrote %s\n", junit.c_str());
        else
            std::printf("[junit] FAILED to write %s\n", junit.c_str());
    }
    return ok ? 0 : 1;
}
