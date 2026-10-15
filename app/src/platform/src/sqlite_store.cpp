#include "railhmi/platform/sqlite_store.h"

#ifdef RAILHMI_HAVE_SQLITE

#include <sqlite3.h>

namespace railhmi {
namespace platform {

SqliteStore::SqliteStore() : db_(nullptr) {}

SqliteStore::~SqliteStore() { close(); }

bool SqliteStore::open(const std::string& path, std::string* err) {
    close();
    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        if (err) *err = std::string("sqlite open failed: ") + (db_ ? sqlite3_errmsg(db_) : "unknown");
        close();
        return false;
    }
    if (!exec("PRAGMA journal_mode=WAL;", err)) return false;
    if (!exec("PRAGMA foreign_keys=ON;", err)) return false;
    return true;
}

void SqliteStore::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SqliteStore::exec(const std::string& sql, std::string* err) {
    if (!db_) { if (err) *err = "db not open"; return false; }
    char* msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &msg);
    if (rc != SQLITE_OK) {
        if (err) *err = std::string("sqlite exec failed: ") + (msg ? msg : sqlite3_errmsg(db_));
        if (msg) sqlite3_free(msg);
        return false;
    }
    return true;
}

bool SqliteStore::query_int(const std::string& sql, long long* out, std::string* err) {
    if (!db_) { if (err) *err = "db not open"; return false; }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        if (err) *err = std::string("sqlite prepare failed: ") + sqlite3_errmsg(db_);
        return false;
    }
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        if (out) *out = sqlite3_column_int64(stmt, 0);
        ok = true;
    } else if (err) {
        *err = std::string("sqlite step failed: ") + sqlite3_errmsg(db_);
    }
    sqlite3_finalize(stmt);
    return ok;
}

}  // namespace platform
}  // namespace railhmi

#endif  // RAILHMI_HAVE_SQLITE
