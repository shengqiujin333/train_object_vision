// railhmi-core 单元测试入口（ITEM-003 骨架；ITEM-008 扩展为完整域）。
// 仅依赖 C++ 标准库 + railhmi_core/railhmi_platform；无 Qt/GStreamer/相机依赖。

#include <cstdio>
#include <cstdlib>
#include <string>

#include "railhmi/core/version.h"
#include "railhmi/platform/file_store.h"
#include "railhmi/platform/log.h"
#ifdef RAILHMI_HAVE_SQLITE
#include "railhmi/platform/sqlite_store.h"
#endif

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const std::string& name, const std::string& detail = "") {
    if (cond) {
        ++g_pass;
        std::printf("[PASS] %s\n", name.c_str());
    } else {
        ++g_fail;
        std::printf("[FAIL] %s%s%s\n", name.c_str(),
                    detail.empty() ? "" : " -- ", detail.c_str());
    }
}

void test_version() {
    using namespace railhmi::core;
    check(std::string(version_string()) == "0.1.0", "version_string == 0.1.0");
    const std::string mode = build_mode();
    check(mode == "Target" || mode == "Preview" || mode == "CoreTests",
          "build_mode is a known mode", mode);
    const std::string desc = build_description();
    check(desc.find("railhmi") != std::string::npos, "build_description contains railhmi", desc);
}

void test_redact() {
    using namespace railhmi::platform;
    check(redact_url("rtsp://admin:secret@192.168.1.65:554/cam") ==
              "rtsp://***:***@192.168.1.65:554/cam",
          "redact_url hides credentials");
    check(redact_url("rtsp://192.168.1.65:554/cam") == "rtsp://192.168.1.65:554/cam",
          "redact_url leaves credential-free url unchanged");
}

void test_file_store() {
    using namespace railhmi::platform;
    const std::string base = "/tmp/railhmi_core_test";
    const std::string path = base + "/nested/probe.txt";
    std::string err;
    const std::string payload = "railhmi-atomic\nline2";
    check(FileStore::write_atomic(path, payload, &err), "FileStore::write_atomic", err);
    err.clear();
    const std::string got = FileStore::read_all(path, &err);
    check(got == payload, "FileStore::read_all round-trip", err);
    check(FileStore::exists(path), "FileStore::exists after write");
    check(!FileStore::exists(base + "/does_not_exist.txt"), "FileStore::exists false for missing");
    std::remove(path.c_str());
}

#ifdef RAILHMI_HAVE_SQLITE
void test_sqlite() {
    using namespace railhmi::platform;
    const std::string path = "/tmp/railhmi_core_test_sqlite.db";
    std::remove(path.c_str());
    {
        SqliteStore db;
        std::string err;
        check(db.open(path, &err), "SqliteStore::open", err);
        check(db.exec("CREATE TABLE IF NOT EXISTS t(id INTEGER PRIMARY KEY, v TEXT);", &err),
              "SqliteStore::exec create table", err);
        check(db.exec("INSERT INTO t(v) VALUES('a');", &err), "SqliteStore::exec insert", err);
        long long n = -1;
        check(db.query_int("SELECT count(*) FROM t;", &n, &err), "SqliteStore::query_int", err);
        check(n == 1, "SqliteStore row count == 1", std::to_string(n));
    }
    {
        SqliteStore db;  // reopen to prove persistence
        std::string err;
        check(db.open(path, &err), "SqliteStore::open (reopen)", err);
        long long n = -1;
        check(db.query_int("SELECT count(*) FROM t;", &n, &err) && n == 1,
              "SqliteStore persistence across reopen");
    }
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}
#endif

}  // namespace

int main() {
    std::printf("== railhmi-core tests (%s) ==\n", railhmi::core::build_description().c_str());
    test_version();
    test_redact();
    test_file_store();
#ifdef RAILHMI_HAVE_SQLITE
    test_sqlite();
#else
    std::printf("[SKIP] sqlite tests (RAILHMI_HAVE_SQLITE not defined)\n");
#endif
    std::printf("== %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
