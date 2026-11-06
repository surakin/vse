#include "../os.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace engine_placeholder::os {

namespace {

// Copies `path` into `buffer` (size kMaxPathBytes) and null-terminates
// it, for POSIX calls that require a C string. Returns nullptr if `path`
// doesn't fit (including the terminator) rather than silently
// truncating.
const char* ToCPath(StringView path, char (&buffer)[kMaxPathBytes]) {
    if (path.Length() >= kMaxPathBytes) {
        return nullptr;
    }
    for (usize i = 0; i < path.Length(); ++i) {
        buffer[i] = path[i];
    }
    buffer[path.Length()] = '\0';
    return buffer;
}

} // namespace

void Exit(int code) {
    ::exit(code);
}

long Write(int fd, const void* buf, usize len) {
    return ::write(fd, buf, len);
}

long Read(int fd, void* buf, usize len) {
    return ::read(fd, buf, len);
}

int Open(StringView path, int flags, int mode) {
    char buf[kMaxPathBytes];
    const char* c_path = ToCPath(path, buf);
    if (c_path == nullptr) {
        return -1;
    }
    return ::open(c_path, flags, mode);
}

int CreateFileForWriting(StringView path, int mode) {
    return Open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int Close(int fd) {
    return ::close(fd);
}

bool EnsureDirectory(StringView path, int mode) {
    char buffer[kMaxPathBytes];
    const char* c_path = ToCPath(path, buffer);
    if (c_path == nullptr || buffer[0] == '\0') {
        return false;
    }
    // Create each missing component from the root down. A component that exists is left as it is.
    for (usize i = 1; buffer[i] != '\0'; ++i) {
        if (buffer[i] != '/') {
            continue;
        }
        buffer[i] = '\0';
        if (::mkdir(buffer, static_cast<mode_t>(mode)) != 0 && errno != EEXIST) {
            return false;
        }
        buffer[i] = '/';
    }
    if (::mkdir(buffer, static_cast<mode_t>(mode)) != 0 && errno != EEXIST) {
        return false;
    }
    struct stat info;
    return ::stat(buffer, &info) == 0 && S_ISDIR(info.st_mode);
}

bool EnsurePrivateDirectory(StringView path) {
    char buffer[kMaxPathBytes];
    if (ToCPath(path, buffer) == nullptr || !EnsureDirectory(path, 0700)) {
        return false;
    }
    // A trailing slash would make lstat follow a symbolic link, so look at the name without it.
    usize length = path.Length();
    while (length > 1 && buffer[length - 1] == '/') {
        buffer[--length] = '\0';
    }
    struct stat info;
    if (::lstat(buffer, &info) != 0 || !S_ISDIR(info.st_mode)) {
        return false; // gone, not a directory, or a symbolic link (even one that points at a directory)
    }
    if (info.st_uid != ::geteuid()) {
        return false;
    }
    if ((info.st_mode & 077) != 0 && ::chmod(buffer, 0700) != 0) {
        return false;
    }
    return ::access(buffer, W_OK | X_OK) == 0;
}

bool GetCrashDumpDirectory(char* out, usize capacity) {
    if (capacity == 0) {
        return false;
    }
    out[0] = '\0';
    // Where per-user state goes: $XDG_STATE_HOME, else ~/.local/state. Both must be absolute paths.
    char base[kMaxPathBytes];
    const char* state_home = std::getenv("XDG_STATE_HOME");
    const char* home = std::getenv("HOME");
    if (state_home != nullptr && state_home[0] == '/') {
        std::snprintf(base, sizeof(base), "%s", state_home);
    } else if (home != nullptr && home[0] == '/') {
        std::snprintf(base, sizeof(base), "%s/.local/state", home);
    } else {
        return false;
    }
    // The application name: ENGINE_APP_NAME, or the executable's file name.
    char app[256];
    const char* configured = std::getenv("ENGINE_APP_NAME");
    if (configured != nullptr && configured[0] != '\0') {
        // Used as one path component: no separators, not a reference to the parent, and not silently cut short.
        if (std::strchr(configured, '/') != nullptr || std::strcmp(configured, ".") == 0 || std::strcmp(configured, "..") == 0 ||
            std::strlen(configured) > 200) {
            return false;
        }
        std::snprintf(app, sizeof(app), "%s", configured);
    } else {
        char exe[kMaxPathBytes];
        const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n <= 0) {
            return false;
        }
        exe[n] = '\0';
        const char* slash = std::strrchr(exe, '/');
        const char* name = slash != nullptr ? slash + 1 : exe;
        if (name[0] == '\0' || std::strlen(name) > 200) {
            return false;
        }
        std::snprintf(app, sizeof(app), "%s", name);
    }
    char path[kMaxPathBytes];
    const int length = std::snprintf(path, sizeof(path), "%s/%s/crashes", base, app);
    if (length < 0 || static_cast<usize>(length) >= sizeof(path) || static_cast<usize>(length) >= capacity) {
        return false;
    }
    if (!EnsurePrivateDirectory(StringView(path))) {
        return false;
    }
    std::memcpy(out, path, static_cast<usize>(length) + 1);
    return true;
}

long Lseek(int fd, long offset, int whence) {
    return ::lseek(fd, offset, whence);
}

void* ReserveMemory(usize size) {
    void* mem = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return mem == MAP_FAILED ? nullptr : mem;
}

void ReleaseMemory(void* addr, usize size) {
    ::munmap(addr, size);
}

u64 MonotonicTimeNanos() {
    struct timespec ts;
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<u64>(ts.tv_sec) * 1'000'000'000ULL + static_cast<u64>(ts.tv_nsec);
}

void SleepNanos(u64 nanos) {
    struct timespec req;
    req.tv_sec = static_cast<time_t>(nanos / 1'000'000'000ULL);
    req.tv_nsec = static_cast<long>(nanos % 1'000'000'000ULL);
    struct timespec rem;
    while (::nanosleep(&req, &rem) == -1 && errno == EINTR) {
        req = rem;
    }
}

DynamicLibraryHandle LoadLibrary(StringView path) {
    char buf[kMaxPathBytes];
    const char* c_path = ToCPath(path, buf);
    if (c_path == nullptr) {
        return nullptr;
    }
    return ::dlopen(c_path, RTLD_NOW | RTLD_LOCAL);
}

void* GetSymbol(DynamicLibraryHandle handle, const char* symbol_name) {
    return ::dlsym(handle, symbol_name);
}

void UnloadLibrary(DynamicLibraryHandle handle) {
    if (handle != nullptr) {
        ::dlclose(handle);
    }
}

} // namespace engine_placeholder::os
