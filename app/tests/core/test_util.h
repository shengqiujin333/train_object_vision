#pragma once

#include <string>
#include <vector>

namespace railhmi_test {

struct TestRecord {
    bool pass = false;
    std::string name;
    std::string detail;
};

extern int g_pass;
extern int g_fail;

std::vector<TestRecord>& records();

void check(bool cond, const std::string& name, const std::string& detail = "");

// 写 JUnit XML（可归档、可被 CI 消费）。成功返回 true。
bool write_junit(const std::string& path, const std::string& suite_name);

}  // namespace railhmi_test
