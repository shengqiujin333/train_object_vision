#pragma once

// railhmi: 最小 SQLite C API 声明（subset）。
//
// 目标板常见只有运行时库 libsqlite3.so.0（无 libsqlite3-dev 的 sqlite3.h 与 .so 符号链接）。
// 本头文件在缺少系统 <sqlite3.h> 时提供 railhmi 使用到的那部分 SQLite C API 声明，
// 使工程可链接已有的 libsqlite3.so.0，而无需安装开发包（不改动系统/rootfs）。
// 若系统存在真正的 <sqlite3.h>，则优先使用系统头（避免重复声明）。
//
// SQLite 的 C ABI 长期稳定；此处仅声明函数原型与常量，不包含任何实现。

#if defined(__has_include)
#  if __has_include(<sqlite3.h>)
#    include <sqlite3.h>
#    define RAILHMI_SQLITE_SYSTEM_HEADER 1
#  endif
#endif

#ifndef RAILHMI_SQLITE_SYSTEM_HEADER

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
typedef void (*sqlite3_destructor_type)(void*);

#define SQLITE_OK          0
#define SQLITE_ROW         100
#define SQLITE_DONE        101
#define SQLITE_NULL        5
#define SQLITE_INTEGER     1
#define SQLITE_FLOAT       2
#define SQLITE_TEXT        3
#define SQLITE_OPEN_READWRITE 0x00000002
#define SQLITE_OPEN_CREATE    0x00000004
#define SQLITE_TRANSIENT   ((sqlite3_destructor_type)-1)

int sqlite3_open(const char* filename, sqlite3** ppDb);
int sqlite3_open_v2(const char* filename, sqlite3** ppDb, int flags, const char* zVfs);
int sqlite3_close(sqlite3* db);
const char* sqlite3_errmsg(sqlite3* db);
int sqlite3_exec(sqlite3* db, const char* sql,
                 int (*callback)(void*, int, char**, char**), void* arg, char** errmsg);
void sqlite3_free(void* p);
int sqlite3_busy_timeout(sqlite3* db, int ms);
int sqlite3_prepare_v2(sqlite3* db, const char* zSql, int nByte,
                       sqlite3_stmt** ppStmt, const char** pzTail);
int sqlite3_step(sqlite3_stmt* pStmt);
int sqlite3_finalize(sqlite3_stmt* pStmt);
int sqlite3_reset(sqlite3_stmt* pStmt);
int sqlite3_clear_bindings(sqlite3_stmt* pStmt);
int sqlite3_bind_int64(sqlite3_stmt* pStmt, int i, long long v);
int sqlite3_bind_double(sqlite3_stmt* pStmt, int i, double v);
int sqlite3_bind_text(sqlite3_stmt* pStmt, int i, const char* z, int n,
                      sqlite3_destructor_type d);
int sqlite3_bind_null(sqlite3_stmt* pStmt, int i);
long long sqlite3_column_int64(sqlite3_stmt* pStmt, int i);
double sqlite3_column_double(sqlite3_stmt* pStmt, int i);
const unsigned char* sqlite3_column_text(sqlite3_stmt* pStmt, int i);
int sqlite3_column_type(sqlite3_stmt* pStmt, int i);
int sqlite3_column_count(sqlite3_stmt* pStmt);
const char* sqlite3_column_name(sqlite3_stmt* pStmt, int i);
long long sqlite3_last_insert_rowid(sqlite3* db);
int sqlite3_changes(sqlite3* db);

#ifdef __cplusplus
}
#endif

#endif  // RAILHMI_SQLITE_SYSTEM_HEADER
