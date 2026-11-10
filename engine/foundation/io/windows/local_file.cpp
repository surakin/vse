// The Windows backend of io::File: a Win32 file handle (Phase 13 Day 151). One of the places allowed to call the Win32 file API directly.
//
// Synchronous for now: Day 152 opens with FILE_FLAG_OVERLAPPED and completes asynchronous requests natively. Reads and writes at an offset
// pass it in an OVERLAPPED on the synchronous handle, so concurrent offset reads of one file do not share a file pointer.
#include "cstr/format.h"

#include "io/file.h"
#include "io/windows/win_path.h"
#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <winioctl.h>
#include "unity/windows_disable.h"

namespace engine_placeholder::io {

namespace {

constexpr usize kMaxTransfer = usize{1} << 30; // one ReadFile or WriteFile moves at most 1 GiB (the count is a DWORD)

using windows::kShareAll;
static_assert(windows::kShareAll == (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE));

OVERLAPPED At(u64 offset) {
    OVERLAPPED o{};
    o.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
    o.OffsetHigh = static_cast<DWORD>(offset >> 32);
    return o;
}

class LocalFile final : public File {
public:
    LocalFile(HANDLE handle, bool readable, bool writable, bool append, bool seekable, bool size_known)
        : handle_(handle), readable_(readable), writable_(writable), append_(append), seekable_(seekable), size_known_(size_known) {}

    ~LocalFile() override {
        WaitForAsyncRequests();
        const Status s = Close();
        if (s.Failed()) {
            char text[96];
            const int n = FormatTo(text, sizeof(text), "closing a file failed: %s (os code %d)", ErrorName(s.error), static_cast<int>(s.os_code));
            Log(LogIo, LogVerbosity::Error, StringView(text, n > 0 ? static_cast<usize>(n) : 0));
        }
    }

    bool IsOpen() const override { return handle_ != INVALID_HANDLE_VALUE; }

    u32 Capabilities() const override {
        return (seekable_ ? kCapSeekable : 0u) | (size_known_ ? kCapSizeKnown : 0u) | (writable_ ? kCapWritable : 0u) |
               (append_ ? kCapAppendOnly : 0u) | kCapAtomicReplace;
    }

    Status Size(u64* out) override {
        if (!IsOpen()) return Status::Of(Error::Invalid);
        if (!size_known_) return Status::Of(Error::Unsupported);
        LARGE_INTEGER size;
        if (!GetFileSizeEx(handle_, &size)) return Status::FromOs(static_cast<i32>(GetLastError()));
        *out = static_cast<u64>(size.QuadPart);
        return Status::Success();
    }

    Status Read(void* buffer, usize length, u64 offset, usize* bytes_read) override {
        *bytes_read = 0;
        if (!IsOpen() || !readable_) return Status::Of(Error::Invalid);
        if (length == 0) return Status::Success();
        const DWORD chunk = static_cast<DWORD>(length < kMaxTransfer ? length : kMaxTransfer);
        OVERLAPPED at = At(offset);
        DWORD got = 0;
        if (!ReadFile(handle_, buffer, chunk, &got, seekable_ ? &at : nullptr)) {
            const DWORD e = GetLastError();
            // The end: past it (an offset read on a disk file), or a pipe whose writer closed. Both read as zero bytes, as on Linux.
            if (e == ERROR_HANDLE_EOF || e == ERROR_BROKEN_PIPE) return Status::Success();
            return Status::FromOs(static_cast<i32>(e));
        }
        *bytes_read = got;
        return Status::Success();
    }

    Status Write(const void* data, usize length, u64 offset) override {
        if (!IsOpen() || !writable_) return Status::Of(Error::Invalid);
        if (append_) return Status::Of(Error::Unsupported);
        return WriteAll(data, length, offset, false);
    }

    Status Append(const void* data, usize length) override {
        if (!IsOpen()) return Status::Of(Error::Invalid);
        if (!append_) return Status::Of(Error::Unsupported);
        return WriteAll(data, length, 0, true);
    }

    Status Flush() override { return IsOpen() ? Status::Success() : Status::Of(Error::Invalid); } // nothing is buffered in the process

    Status Sync() override {
        if (!IsOpen()) return Status::Of(Error::Invalid);
        return FlushFileBuffers(handle_) ? Status::Success() : Status::FromOs(static_cast<i32>(GetLastError()));
    }

    Status Truncate(u64 size) override {
        if (!IsOpen() || !writable_) return Status::Of(Error::Invalid);
        LARGE_INTEGER current;
        if (!GetFileSizeEx(handle_, &current)) return Status::FromOs(static_cast<i32>(GetLastError()));
        if (size > static_cast<u64>(current.QuadPart) && !sparse_) {
            // Growing a file leaves a hole, as ftruncate does on Linux: without the sparse flag NTFS would write zeros up to the next write
            // past the old end (gigabytes for a large offset). A file system without sparse files (FAT) refuses, and the file grows plainly.
            DWORD unused = 0;
            sparse_ = DeviceIoControl(handle_, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &unused, nullptr) != FALSE;
        }
        FILE_END_OF_FILE_INFO end{};
        end.EndOfFile.QuadPart = static_cast<LONGLONG>(size);
        if (!SetFileInformationByHandle(handle_, FileEndOfFileInfo, &end, sizeof(end))) return Status::FromOs(static_cast<i32>(GetLastError()));
        return Status::Success();
    }

    Status Close() override {
        if (!IsOpen()) return Status::Success();
        HANDLE h = handle_;
        handle_ = INVALID_HANDLE_VALUE; // closed even when CloseHandle reports an error: the handle is gone either way
        return CloseHandle(h) ? Status::Success() : Status::FromOs(static_cast<i32>(GetLastError()));
    }

private:
    // Writes everything or fails. `append` positions every write at the end (Offset and OffsetHigh all ones), atomically against other
    // appenders, like O_APPEND.
    Status WriteAll(const void* data, usize length, u64 offset, bool append) {
        const u8* src = static_cast<const u8*>(data);
        usize done = 0;
        while (done < length) {
            const DWORD chunk = static_cast<DWORD>(length - done < kMaxTransfer ? length - done : kMaxTransfer);
            OVERLAPPED at = At(offset + done);
            if (append) {
                at.Offset = 0xFFFFFFFFu;
                at.OffsetHigh = 0xFFFFFFFFu;
            }
            DWORD put = 0;
            if (!WriteFile(handle_, src + done, chunk, &put, (seekable_ || append) ? &at : nullptr)) {
                return Status::FromOs(static_cast<i32>(GetLastError()));
            }
            if (put == 0) return Status::Of(Error::Io); // no progress: do not spin
            done += put;
        }
        return Status::Success();
    }

    HANDLE handle_;
    bool readable_;
    bool writable_;
    bool append_;
    bool seekable_;
    bool size_known_;
    bool sparse_ = false;
};

// The CreateFileW disposition for a mode and its flags (O_CREAT and O_EXCL). Truncation (O_TRUNC) is not a disposition here but a
// separate step on the open handle: CREATE_ALWAYS and TRUNCATE_EXISTING refuse a hidden or system file unless the call repeats its
// attributes, and CREATE_ALWAYS replaces the attributes; O_TRUNC keeps the file and empties it.
DWORD Disposition(OpenMode mode, u32 flags) {
    const bool create = (flags & kOpenCreate) != 0 || mode == OpenMode::Write || mode == OpenMode::Append;
    if ((flags & kOpenCreateNew) != 0) return CREATE_NEW;
    return create ? OPEN_ALWAYS : OPEN_EXISTING;
}

bool Truncates(OpenMode mode, u32 flags) {
    return ((flags & kOpenTruncate) != 0 || mode == OpenMode::Write) && mode != OpenMode::Read && (flags & kOpenCreateNew) == 0;
}

} // namespace

Status File::Open(const Path& path, OpenMode mode, u32 flags, FilePtr* out) {
    if (mode == OpenMode::Read && (flags & (kOpenCreate | kOpenCreateNew | kOpenTruncate)) != 0) return Status::Of(Error::Invalid);
    windows::WidePath wide;
    if (Status s = windows::ToWin32(path, &wide); s.Failed()) return s;
    DWORD access = 0;
    switch (mode) {
    case OpenMode::Read: access = GENERIC_READ; break;
    case OpenMode::Write:
    case OpenMode::Append: access = GENERIC_WRITE; break;
    case OpenMode::ReadWrite: access = GENERIC_READ | GENERIC_WRITE; break;
    }
    const DWORD disposition = Disposition(mode, flags);
    HANDLE h = CreateFileW(wide.text, access, kShareAll, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        // A directory cannot be opened as a file: Win32 says "access denied". With CREATE_NEW the name exists, which is what O_EXCL says.
        if (e == ERROR_ACCESS_DENIED) {
            const DWORD attributes = GetFileAttributesW(wide.text);
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                return Status{disposition == CREATE_NEW ? Error::AlreadyExists : Error::IsADirectory, static_cast<i32>(e)};
            }
        }
        return windows::RefineNotFound(wide, e);
    }
    const DWORD type = GetFileType(h);
    const bool disk = type == FILE_TYPE_DISK;
    if (disk && Truncates(mode, flags)) {
        FILE_END_OF_FILE_INFO end{};
        if (!SetFileInformationByHandle(h, FileEndOfFileInfo, &end, sizeof(end))) {
            const Status s = Status::FromOs(static_cast<i32>(GetLastError()));
            CloseHandle(h);
            return s;
        }
    }
    FilePtr file = MakeUnique<LocalFile>(h, mode != OpenMode::Write && mode != OpenMode::Append, mode != OpenMode::Read,
                                         mode == OpenMode::Append, disk, disk);
    if (!file) {
        CloseHandle(h);
        return Status::Of(Error::OutOfMemory);
    }
    *out = static_cast<FilePtr&&>(file);
    return Status::Success();
}

} // namespace engine_placeholder::io
