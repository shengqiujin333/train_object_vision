#include "railhmi/platform/log.h"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace railhmi {
namespace platform {

namespace {
const char* level_name(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}
}  // namespace

Logger::Logger() : level_(LogLevel::Info) {}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::set_level(LogLevel level) { level_ = level; }
LogLevel Logger::level() const { return level_; }

void Logger::log(LogLevel level, const std::string& message) {
    if (static_cast<int>(level) < static_cast<int>(level_)) return;
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()).count() % 1000;
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char base[24];
    std::strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &tm);
    char ts[40];
    std::snprintf(ts, sizeof(ts), "%s.%03d", base, static_cast<int>(ms));
    std::fprintf(stdout, "%s %-5s %s\n", ts, level_name(level), message.c_str());
    std::fflush(stdout);
}

std::string redact_url(const std::string& url) {
    // 替换 scheme://user:pass@host 中的凭据部分
    const std::string sep = "://";
    auto scheme = url.find(sep);
    if (scheme == std::string::npos) return url;
    auto at = url.find('@', scheme + sep.size());
    if (at == std::string::npos) return url;
    auto colon = url.find(':', scheme + sep.size());
    if (colon == std::string::npos || colon > at) return url;
    return url.substr(0, scheme + sep.size()) + "***:***@" + url.substr(at + 1);
}

}  // namespace platform
}  // namespace railhmi
