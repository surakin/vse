// The Linux backend of io::File: a plain file descriptor. One of the places allowed to call the POSIX file API directly.
#include <cerrno>
#include <cstdio>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "io/file.h"

namespace engine_placeholder::io {

namespace {

constexpr usize kMaxTransfer = usize{1} << 30; // one system call moves at most 1 GiB

class LocalFile final : public File {
public:
    LocalFile(int fd, bool readable, bool writable, bool append, bool seekable, bool size_known)
        : fd_(fd), readable_(readable), writable_(writable), append_(append), seekable_(seekable), size_known_(size_known) {}

    ~LocalFile() override {
        const Status s = Close();
        if (s.Failed()) {
            char text[96];
            const int n = std::snprintf(text, sizeof(text), "closing a file failed: %s (os code %d)", ErrorName(s.error), static_cast<int>(s.os_code));
            Log(LogIo, LogVerbosity::Error, StringView(text, n > 0 ? static_cast<usize>(n) : 0));
        }
    }

    bool IsOpen() const override { return fd_ >= 0; }

    u32 Capabilities() const override {
        return (seekable_ ? kCapSeekable : 0u) | (size_known_ ? kCapSizeKnown : 0u) | (writable_ ? kCapWritable : 0u) |
               (append_ ? kCapAppendOnly : 0u) | kCapAtomicReplace;
    }

    Status Size(u64* out) override {
        if (fd_ < 0) return Status::Of(Error::Invalid);
        if (!size_known_) return Status::Of(Error::Unsupported);
        struct stat st;
        if (::fstat(fd_, &st) != 0) return Status::FromOs(errno);
        *out = static_cast<u64>(st.st_size);
        return Status::Success();
    }

    Status Read(void* buffer, usize length, u64 offset, usize* bytes_read) override {
        *bytes_read = 0;
        if (fd_ < 0 || !readable_) return Status::Of(Error::Invalid);
        if (length == 0) return Status::Success();
        const usize chunk = length < kMaxTransfer ? length : kMaxTransfer;
        for (;;) {
            const ssize_t n = seekable_ ? ::pread(fd_, buffer, chunk, static_cast<off_t>(offset)) : ::read(fd_, buffer, chunk);
            if (n >= 0) {
                *bytes_read = static_cast<usize>(n);
                return Status::Success();
            }
            if (errno != EINTR) return Status::FromOs(errno);
        }
    }

    Status Write(const void* data, usize length, u64 offset) override {
        if (fd_ < 0 || !writable_) return Status::Of(Error::Invalid);
        if (append_) return Status::Of(Error::Unsupported);
        const u8* src = static_cast<const u8*>(data);
        usize done = 0;
        while (done < length) {
            const usize chunk = length - done < kMaxTransfer ? length - done : kMaxTransfer;
            const ssize_t n = seekable_ ? ::pwrite(fd_, src + done, chunk, static_cast<off_t>(offset + done)) : ::write(fd_, src + done, chunk);
            if (n < 0) {
                if (errno == EINTR) continue;
                return Status::FromOs(errno);
            }
            if (n == 0) return Status::Of(Error::Io); // no progress: do not spin
            done += static_cast<usize>(n);
        }
        return Status::Success();
    }

    Status Append(const void* data, usize length) override {
        if (fd_ < 0) return Status::Of(Error::Invalid);
        if (!append_) return Status::Of(Error::Unsupported);
        const u8* src = static_cast<const u8*>(data);
        usize done = 0;
        while (done < length) {
            const usize chunk = length - done < kMaxTransfer ? length - done : kMaxTransfer;
            const ssize_t n = ::write(fd_, src + done, chunk);
            if (n < 0) {
                if (errno == EINTR) continue;
                return Status::FromOs(errno);
            }
            if (n == 0) return Status::Of(Error::Io); // no progress: do not spin
            done += static_cast<usize>(n);
        }
        return Status::Success();
    }

    Status Flush() override { return fd_ < 0 ? Status::Of(Error::Invalid) : Status::Success(); }

    Status Sync() override {
        if (fd_ < 0) return Status::Of(Error::Invalid);
        return ::fsync(fd_) == 0 ? Status::Success() : Status::FromOs(errno);
    }

    Status Truncate(u64 size) override {
        if (fd_ < 0 || !writable_) return Status::Of(Error::Invalid);
        return ::ftruncate(fd_, static_cast<off_t>(size)) == 0 ? Status::Success() : Status::FromOs(errno);
    }

    Status Close() override {
        if (fd_ < 0) return Status::Success();
        const int fd = fd_;
        fd_ = -1; // closed even when close() reports an error: the descriptor is gone either way
        return ::close(fd) == 0 ? Status::Success() : Status::FromOs(errno);
    }

private:
    int fd_;
    bool readable_;
    bool writable_;
    bool append_;
    bool seekable_;
    bool size_known_;
};

} // namespace

Status File::Open(const Path& path, OpenMode mode, u32 flags, UniquePtr<File>* out) {
    char native[kMaxPathBytes + 1];
    if (!path.ToNative(native, sizeof(native))) return Status::Of(Error::TooLong);
    if (mode == OpenMode::Read && (flags & (kOpenCreate | kOpenCreateNew | kOpenTruncate)) != 0) return Status::Of(Error::Invalid);
    int oflags = O_CLOEXEC | O_NOCTTY;
    switch (mode) {
    case OpenMode::Read: oflags |= O_RDONLY; break;
    case OpenMode::Write: oflags |= O_WRONLY | O_CREAT | O_TRUNC; break;
    case OpenMode::Append: oflags |= O_WRONLY | O_CREAT | O_APPEND; break;
    case OpenMode::ReadWrite: oflags |= O_RDWR; break;
    }
    if ((flags & kOpenCreate) != 0) oflags |= O_CREAT;
    if ((flags & kOpenCreateNew) != 0) oflags |= O_CREAT | O_EXCL;
    if ((flags & kOpenTruncate) != 0 && mode != OpenMode::Read) oflags |= O_TRUNC;
    const int fd = ::open(native, oflags, 0644);
    if (fd < 0) return Status::FromOs(errno);
    struct stat st;
    if (::fstat(fd, &st) != 0) {
        const Status s = Status::FromOs(errno);
        ::close(fd);
        return s;
    }
    if (S_ISDIR(st.st_mode)) {
        ::close(fd);
        return Status::Of(Error::IsADirectory);
    }
    const bool seekable = S_ISREG(st.st_mode) || S_ISBLK(st.st_mode);
    UniquePtr<File> file = MakeUnique<LocalFile>(fd, mode != OpenMode::Write && mode != OpenMode::Append, mode != OpenMode::Read,
                                                 mode == OpenMode::Append, seekable, S_ISREG(st.st_mode));
    if (!file) {
        ::close(fd);
        return Status::Of(Error::OutOfMemory);
    }
    *out = static_cast<UniquePtr<File>&&>(file);
    return Status::Success();
}

} // namespace engine_placeholder::io
