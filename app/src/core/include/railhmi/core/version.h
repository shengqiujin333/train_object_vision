#pragma once

// railhmi-core: 纯业务逻辑层，只依赖 C++ 标准库。
// 不包含 Qt / GStreamer / 相机 / SQLite / 板端库 —— 保证可在任意有 C++ 编译器的主机上单元测试。

#include <string>

namespace railhmi {
namespace core {

// 应用版本（单一来源，供 UI/日志/打包引用）。
const char* version_string();

// 构建档位：由构建系统注入（Target|Preview|CoreTests）。
const char* build_mode();

// 是否启用模拟输入（preview / 无相机环境）。
bool simulation_enabled();

// 一行构建描述，用于启动日志与自检输出。
std::string build_description();

}  // namespace core
}  // namespace railhmi
