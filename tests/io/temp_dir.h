#pragma once

#include <cstdlib>
#include <cstring>
#include <string>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "io/file_system.h"

namespace io_test {

using namespace engine_placeholder;

inline std::string Str(StringView v) { return std::string(v.Data() == nullptr ? "" : v.Data(), v.Length()); }

/// Writes `text` to `path` through the POSIX API (these helpers are independent of the code under test).
inline bool WriteText(const io::Path& path, const char* text) {
    char p[io::kMaxPathBytes + 1];
    if (!path.ToNative(p, sizeof(p))) return false;
    const int fd = ::open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    const size_t n = std::strlen(text);
    const bool ok = ::write(fd, text, n) == static_cast<ssize_t>(n);
    ::close(fd);
    return ok;
}

/// Reads up to 4 KiB of a file the same way.
inline std::string ReadText(const io::Path& path) {
    char p[io::kMaxPathBytes + 1];
    if (!path.ToNative(p, sizeof(p))) return "";
    const int fd = ::open(p, O_RDONLY);
    if (fd < 0) return "<unreadable>";
    char buf[4096];
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    ::close(fd);
    return std::string(buf, n > 0 ? static_cast<size_t>(n) : 0);
}

/// Gives the owner full access to a tree (not following links) so it can be removed after a test made parts read-only.
inline void MakeWritable(const std::string& path, int depth = 0) {
    struct stat st;
    if (depth > 64 || ::lstat(path.c_str(), &st) != 0 || S_ISLNK(st.st_mode)) return;
    ::chmod(path.c_str(), st.st_mode | S_IRWXU);
    if (!S_ISDIR(st.st_mode)) return;
    if (DIR* d = ::opendir(path.c_str())) {
        while (const dirent* e = ::readdir(d)) {
            if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0) {
                MakeWritable(path + "/" + e->d_name, depth + 1);
            }
        }
        ::closedir(d);
    }
}

/// A directory created for one test and removed (with everything in it) afterwards.
class TempDir {
public:
    TempDir() {
        io::Path tmp;
        io::TempDirectory(&tmp);
        if (io::CreateTempDirectory(tmp, "re_io_test", &path_).Failed()) {
            std::abort();
        }
    }
    ~TempDir() {
        MakeWritable(Native());
        io::RemoveAll(path_);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const io::Path& Path() const { return path_; }
    io::Path operator/(StringView name) const { return path_.Join(name); }
    std::string Native() const { return std::string(path_.View().Data(), path_.View().Length()); }

private:
    io::Path path_;
};

} // namespace io_test
