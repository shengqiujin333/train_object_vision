#pragma once

#include <string>

namespace railhmi {
namespace platform {

// 文件存储：原子写入（先写临时文件再重命名），用于断电保护（DATA-002）。
// 仅依赖 C++ 标准库。
class FileStore {
public:
    // 原子写入：写 <path>.tmp.<pid> 后 rename 为 path。
    // 返回 true 成功；失败时 err 填入原因。
    static bool write_atomic(const std::string& path, const std::string& content, std::string* err = nullptr);

    // 读取整个文件；失败返回空串并置 err。
    static std::string read_all(const std::string& path, std::string* err = nullptr);

    static bool exists(const std::string& path);

    // 确保父目录存在（递归创建）。
    static bool ensure_parent_dir(const std::string& path, std::string* err = nullptr);
};

}  // namespace platform
}  // namespace railhmi
