#include "test_util.h"

#include <cstdio>

namespace railhmi_test {

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const std::string& name, const std::string& detail) {
    if (cond) {
        ++g_pass;
        std::printf("[PASS] %s\n", name.c_str());
    } else {
        ++g_fail;
        std::printf("[FAIL] %s%s%s\n", name.c_str(),
                    detail.empty() ? "" : " -- ", detail.c_str());
    }
}

}  // namespace railhmi_test
