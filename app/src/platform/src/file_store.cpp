#include "railhmi/platform/file_store.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#include <process.h>
#define RAILHMI_GETPID _getpid
#else
#include <unistd.h>
#define RAILHMI_GETPID getpid
#endif

namespace railhmi {
namespace platform {

namespace {
bool make_dir(const std::string& d, std::string* err) {
    if (d.empty() || d == "/" || d == ".") return true;
#if defined(_WIN32)
    int rc = _mkdir(d.c_str());
#else
    int rc = mkdir(d.c_str(), 0755);
#endif
    if (rc == 0 || errno == EEXIST) return true;
    if (err) *err = "mkdir failed: " + d + ": " + std::strerror(errno);
    return false;
}
}  // namespace

bool FileStore::ensure_parent_dir(const std::string& path, std::string* err) {
    auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return true;
    std::string dir = path.substr(0, slash);
    if (dir.empty()) return true;
    for (std::size_t p = 1; p <= dir.size(); ++p) {
        if (p == dir.size() || dir[p] == '/' || dir[p] == '\\') {
            std::string sub = dir.substr(0, p);
            if (!sub.empty() && sub != "/" && sub != ".") {
                if (!make_dir(sub, err)) return false;
            }
        }
    }
    return true;
}

bool FileStore::write_atomic(const std::string& path, const std::string& content, std::string* err) {
    if (!ensure_parent_dir(path, err)) return false;
    std::ostringstream tmp;
    tmp << path << ".tmp." << RAILHMI_GETPID();
    const std::string tmp_path = tmp.str();
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out) { if (err) *err = "open tmp failed: " + tmp_path; return false; }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) { if (err) *err = "write tmp failed: " + tmp_path; return false; }
    }
    if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
        if (err) *err = "rename failed: " + tmp_path + " -> " + path + ": " + std::strerror(errno);
        std::remove(tmp_path.c_str());
        return false;
    }
    return true;
}

std::string FileStore::read_all(const std::string& path, std::string* err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { if (err) *err = "open failed: " + path; return std::string(); }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool FileStore::exists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

}  // namespace platform
}  // namespace railhmi
