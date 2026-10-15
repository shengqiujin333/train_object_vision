// railhmi-core/platform 基础测试（版本/脱敏/原子文件/SQLite）。

#include "test_util.h"

#include <cstdio>
#include <string>

#include "railhmi/core/version.h"
#include "railhmi/platform/file_store.h"
#include "railhmi/platform/log.h"
#ifdef RAILHMI_HAVE_SQLITE
#include "railhmi/platform/sqlite_store.h"
#endif

namespace {

void test_version() {
    using namespace railhmi::core;
    railhmi_test::check(std::string(version_string()) == "0.1.0", "version_string == 0.1.0");
    const std::string mode = build_mode();
    railhmi_test::check(mode == "Target" || mode == "Preview" || mode == "CoreTests" ||
                            mode == "MediaCheck",
                        "build_mode is a known mode", mode);
    const std::string desc = build_description();
    railhmi_test::check(desc.find("railhmi") != std::string::npos,
                        "build_description contains railhmi", desc);
}

void test_redact() {
    using namespace railhmi::platform;
    railhmi_test::check(redact_url("rtsp://admin:secret@192.168.1.65:554/cam") ==
                            "rtsp://***:***@192.168.1.65:554/cam",
                        "redact_url hides credentials");
    railhmi_test::check(redact_url("rtsp://192.168.1.65:554/cam") ==
                            "rtsp://192.168.1.65:554/cam",
                        "redact_url leaves credential-free url unchanged");
}

void test_file_store() {
    using namespace railhmi::platform;
    const std::string base = "/tmp/railhmi_core_test";
    const std::string path = base + "/nested/probe.txt";
    std::string err;
    const std::string payload = "railhmi-atomic\nline2";
    railhmi_test::check(FileStore::write_atomic(path, payload, &err),
                        "FileStore::write_atomic", err);
    err.clear();
    const std::string got = FileStore::read_all(path, &err);
    railhmi_test::check(got == payload, "FileStore::read_all round-trip", err);
    railhmi_test::check(FileStore::exists(path), "FileStore::exists after write");
    railhmi_test::check(!FileStore::exists(base + "/does_not_exist.txt"),
                        "FileStore::exists false for missing");
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
        railhmi_test::check(db.open(path, &err), "SqliteStore::open", err);
        railhmi_test::check(db.exec("CREATE TABLE IF NOT EXISTS t(id INTEGER PRIMARY KEY, v TEXT);", &err),
                            "SqliteStore::exec create table", err);
        railhmi_test::check(db.exec("INSERT INTO t(v) VALUES('a');", &err),
                            "SqliteStore::exec insert", err);
        long long n = -1;
        railhmi_test::check(db.query_int("SELECT count(*) FROM t;", &n, &err),
                            "SqliteStore::query_int", err);
        railhmi_test::check(n == 1, "SqliteStore row count == 1", std::to_string(n));
    }
    {
        SqliteStore db;
        std::string err;
        railhmi_test::check(db.open(path, &err), "SqliteStore::open (reopen)", err);
        long long n = -1;
        railhmi_test::check(db.query_int("SELECT count(*) FROM t;", &n, &err) && n == 1,
                            "SqliteStore persistence across reopen");
    }
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}
#endif

}  // namespace

void run_platform_tests() {
    test_version();
    test_redact();
    test_file_store();
#ifdef RAILHMI_HAVE_SQLITE
    test_sqlite();
#else
    std::printf("[SKIP] sqlite tests (RAILHMI_HAVE_SQLITE not defined)\n");
#endif
}
