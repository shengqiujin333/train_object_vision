// railhmi-core 单元测试入口（ITEM-003 骨架；ITEM-004 起扩展）。
// 仅依赖 C++ 标准库 + railhmi_core/railhmi_platform；无 Qt/GStreamer/相机依赖。

#include <cstdio>

#include "railhmi/core/version.h"
#include "test_util.h"

void run_platform_tests();
void run_config_tests();
void run_stream_tests();

int main() {
    std::printf("== railhmi-core tests (%s) ==\n",
                railhmi::core::build_description().c_str());
    run_platform_tests();
    run_config_tests();
    run_stream_tests();
    std::printf("== %d passed, %d failed ==\n", railhmi_test::g_pass, railhmi_test::g_fail);
    return railhmi_test::g_fail == 0 ? 0 : 1;
}
