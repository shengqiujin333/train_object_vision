#pragma once

#include <string>

namespace railhmi_test {

extern int g_pass;
extern int g_fail;

void check(bool cond, const std::string& name, const std::string& detail = "");

}  // namespace railhmi_test
