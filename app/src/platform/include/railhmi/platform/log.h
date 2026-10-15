#pragma once

// railhmi-platform: 基础设施（日志/文件存储/持久化）。
// 只依赖 C++ 标准库；SQLite 部件可选（RAILHMI_HAVE_SQLITE）。
// 不含 Qt/GStreamer/相机，保证 core+platform 可在最小工具链上构建与测试。

#include <string>

namespace railhmi {
namespace platform {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

// 极简结构化日志：单行 "ts level message"，便于 grep/归档。
// 由 storage/日志任务（ITEM-019）扩展为按日滚动文件；此处提供接口与默认 stdout 实现。
class Logger {
public:
    static Logger& instance();
    void set_level(LogLevel level);
    LogLevel level() const;
    void log(LogLevel level, const std::string& message);

    void debug(const std::string& m) { log(LogLevel::Debug, m); }
    void info(const std::string& m) { log(LogLevel::Info, m); }
    void warn(const std::string& m) { log(LogLevel::Warn, m); }
    void error(const std::string& m) { log(LogLevel::Error, m); }

private:
    Logger();
    LogLevel level_;
};

// 对含凭据的 RTSP/URL 做脱敏（日志安全）：把 user:pass@ 替换为 ***:***@。
std::string redact_url(const std::string& url);

}  // namespace platform
}  // namespace railhmi
