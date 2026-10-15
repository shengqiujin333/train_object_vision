#pragma once

// SqliteStore 仅在找到 SQLite3 开发包时编译（RAILHMI_HAVE_SQLITE）。
// 单写线程 + WAL 的持久化实现由 ITEM-007/008 落地；此处提供骨架接口。

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

    // 打开数据库（WAL）。成功返回 true，失败填入 err。
    bool open(const std::string& path, std::string* err = nullptr);
    void close();

    // 执行无返回语句（DDL/DML）。成功返回 true。
    bool exec(const std::string& sql, std::string* err = nullptr);

    // 执行返回单个整数的查询（如 SELECT count(*)）。
    bool query_int(const std::string& sql, long long* out, std::string* err = nullptr);

    bool is_open() const { return db_ != nullptr; }

private:
    sqlite3* db_;
};

}  // namespace platform
}  // namespace railhmi

#endif  // RAILHMI_HAVE_SQLITE
