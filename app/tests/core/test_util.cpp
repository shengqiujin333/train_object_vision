#include "test_util.h"

#include <cstdio>
#include <fstream>

namespace railhmi_test {

int g_pass = 0;
int g_fail = 0;

std::vector<TestRecord>& records() {
    static std::vector<TestRecord> r;
    return r;
}

void check(bool cond, const std::string& name, const std::string& detail) {
    records().push_back(TestRecord{cond, name, detail});
    if (cond) {
        ++g_pass;
        std::printf("[PASS] %s\n", name.c_str());
    } else {
        ++g_fail;
        std::printf("[FAIL] %s%s%s\n", name.c_str(),
                    detail.empty() ? "" : " -- ", detail.c_str());
    }
}

namespace {
std::string xml_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&apos;"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n') o += ' ';
                else o += c;
        }
    }
    return o;
}
}  // namespace

bool write_junit(const std::string& path, const std::string& suite_name) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    const std::size_t total = records().size();
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<testsuites tests=\"" << total << "\" failures=\"" << g_fail << "\">\n";
    out << "  <testsuite name=\"" << xml_escape(suite_name) << "\" tests=\"" << total
        << "\" failures=\"" << g_fail << "\">\n";
    for (const auto& r : records()) {
        out << "    <testcase classname=\"railhmi-core\" name=\"" << xml_escape(r.name) << "\"";
        if (r.pass) {
            out << "/>\n";
        } else {
            out << ">\n      <failure message=\"" << xml_escape(r.detail)
                << "\">failed</failure>\n    </testcase>\n";
        }
    }
    out << "  </testsuite>\n</testsuites>\n";
    return static_cast<bool>(out);
}

}  // namespace railhmi_test
