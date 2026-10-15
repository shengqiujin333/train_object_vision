#pragma once

// SqliteStore 仅在找到 SQLite（dev 包或运行时库 + 项目 shim 头）时编译（RAILHMI_HAVE_SQLITE）。
// 低层封装：打开（WAL）、执行、查询；高层仓储见 repository.h（SqliteRepository）。

#ifdef RAILHMI_HAVE_SQLITE

#include <string>

struct sqlite3;  // 前置声明，避免头文件暴露 sqlite3.h

namespace railhmi {
namespace platform {

class SqliteStore {
public:
    SqliteStore();
    ~SqliteStore();

    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;

    // 打开数据库（WAL + busy_timeout）。成功返回 true，失败填入 err。
    bool open(const std::string& path, std::string* err = nullptr);
    void close();

    bool exec(const std::string& sql, std::string* err = nullptr);
    bool query_int(const std::string& sql, long long* out, std::string* err = nullptr);
    std::string journal_mode(std::string* err = nullptr);

    bool is_open() const { return db_ != nullptr; }
    sqlite3* handle() const { return db_; }

private:
    sqlite3* db_;
};

}  // namespace platform
}  // namespace railhmi

#endif  // RAILHMI_HAVE_SQLITE
