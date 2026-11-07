#pragma once

#include <atomic>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "io/directory.h"
#include "io/file_system.h"
#include "test_text.h"

namespace io_test {

using namespace engine_placeholder;

inline engine_test::Text Str(StringView v) { return engine_test::MakeText(v.Data() == nullptr ? StringView() : v); }

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
inline engine_test::Text ReadText(const io::Path& path) {
    char p[io::kMaxPathBytes + 1];
    if (!path.ToNative(p, sizeof(p))) return engine_test::MakeText();
    const int fd = ::open(p, O_RDONLY);
    if (fd < 0) return engine_test::MakeText("<unreadable>");
    char buf[4096];
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    ::close(fd);
    return engine_test::MakeText(StringView(buf, n > 0 ? static_cast<size_t>(n) : 0));
}

/// Gives the owner full access to a tree (not following links) so it can be removed after a test made parts read-only.
inline void MakeWritable(const engine_test::Text& path, int depth = 0) {
    struct stat st;
    if (depth > 64 || ::lstat(path.CStr(), &st) != 0 || S_ISLNK(st.st_mode)) return;
    ::chmod(path.CStr(), st.st_mode | S_IRWXU);
    if (!S_ISDIR(st.st_mode)) return;
    if (DIR* d = ::opendir(path.CStr())) {
        while (const dirent* e = ::readdir(d)) {
            if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0) {
                MakeWritable(engine_test::Concat(path.View(), "/", StringView(e->d_name)), depth + 1);
            }
        }
        ::closedir(d);
    }
}

/// Removes the `re_io_test.*` directories an interrupted run left behind: only those not modified for an hour, so a test program running
/// at the same time keeps its own. Called once per process, with the directory just created as the clock (its modification time is "now").
inline void SweepStaleTempDirs(const io::Path& tmp, const io::Path& fresh) {
    io::FileInfo now_info;
    if (io::Stat(fresh, &now_info).Failed()) return;
    constexpr i64 kStaleNs = i64{3600} * 1000000000;
    Vector<io::DirectoryItem> items;
    if (io::ListDirectory(tmp, false, &items).Failed()) return;
    for (usize i = 0; i < items.Size(); ++i) {
        if (items[i].type != io::FileType::Directory || !items[i].name.View().StartsWith("re_io_test.")) continue;
        const io::Path candidate = tmp.Join(items[i].name.View());
        io::FileInfo info;
        if (candidate == fresh || io::Lstat(candidate, &info).Failed() || now_info.modified_unix_ns - info.modified_unix_ns < kStaleNs) continue;
        MakeWritable(engine_test::MakeText(candidate.View()));
        io::RemoveAll(candidate);
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
        static std::atomic<bool> swept{false}; // the first directory of the process cleans up after earlier, interrupted runs
        if (!swept.exchange(true)) SweepStaleTempDirs(tmp, path_);
    }
    ~TempDir() {
        MakeWritable(Native());
        io::RemoveAll(path_);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const io::Path& Path() const { return path_; }
    io::Path operator/(StringView name) const { return path_.Join(name); }
    engine_test::Text Native() const { return engine_test::MakeText(path_.View()); }

private:
    io::Path path_;
};

} // namespace io_test
