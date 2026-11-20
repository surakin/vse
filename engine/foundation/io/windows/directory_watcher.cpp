// Phase 27 Day 473: the Windows directory watcher, on ReadDirectoryChangesW. One of the places allowed to call the
// Win32 file API directly (the Windows file backend, tools/file_access_check/allowlist.txt).
//
// One overlapped read is outstanding at a time, into a 64 KiB buffer the watcher owns; each completion is parsed and
// the read is armed again. Close (the destructor) cancels the read and waits for it to end, so the system never writes
// into the buffer after it is freed.
#include "containers/array.h"
#include "containers/vector.h"
#include "io/detail/watch_queue.h"
#include "io/directory_watcher.h"
#include "io/windows/win_path.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"
#include "sync/mutex.h"

namespace vse::io
{

namespace
{

// The size of the read buffer: 64 KiB, the most ReadDirectoryChangesW accepts for a directory on a network share.
constexpr usize kBufferBytes = usize{64} << 10;

// What the read asks for: names (files and directories), writes, sizes and creations.
constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_CREATION;

class ReadDirectoryChangesWatcher final : public DirectoryWatcher
{
public:
    ~ReadDirectoryChangesWatcher() override { Close(); }

    Status Start(const Path& directory)
    {
        windows::WidePath wide;
        if (Status s = windows::ToWin32(directory, &wide); s.Failed())
        {
            return s;
        }
        directory_ = CreateFileW(wide.text, FILE_LIST_DIRECTORY, windows::kShareAll, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (directory_ == INVALID_HANDLE_VALUE)
        {
            return windows::LastError(wide);
        }
        // FILE_FLAG_BACKUP_SEMANTICS opens a file as well as a directory.
        FILE_BASIC_INFO basic{};
        if (!GetFileInformationByHandleEx(directory_, FileBasicInfo, &basic, sizeof(basic)))
        {
            return Status::FromOs(static_cast<i32>(GetLastError()));
        }
        if ((basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            return Status::Of(Error::NotADirectory);
        }
        completed_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // manual reset: the overlapped read's event
        wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);      // auto reset: one Wake ends one Wait
        if (completed_ == nullptr || wake_ == nullptr)
        {
            return Status::FromOs(static_cast<i32>(GetLastError()));
        }
        buffer_.Resize(kBufferBytes / sizeof(u64));  // DWORD-aligned, as the records need
        return Arm();
    }

    Status Poll(Vector<WatchEvent>* out) override
    {
        LockGuard<Mutex> lock(mutex_);
        Drain();
        queue_.Take(out);
        return gone_reported_ ? Status::Of(Error::Invalid) : Status::Success();
    }

    bool Wait(u32 timeout_ms) override
    {
        bool armed = false;
        {
            LockGuard<Mutex> lock(mutex_);
            Drain();
            if (!queue_.Empty())
            {
                return true;
            }
            armed = pending_;
        }
        // Once the watch has ended no read is outstanding and the completion event stays signaled: wait for a wake.
        const Array<HANDLE, 2> handles = {wake_, completed_};
        WaitForMultipleObjects(armed ? 2 : 1, handles.Data(), FALSE, timeout_ms);
        LockGuard<Mutex> lock(mutex_);
        Drain();
        return !queue_.Empty();
    }

    void Wake() override { SetEvent(wake_); }

    bool IsNative() const override { return true; }

private:
    // Starts the next read. The caller holds the mutex (or is Start, before anyone else can see the watcher).
    Status Arm()
    {
        overlapped_ = OVERLAPPED{};
        ResetEvent(completed_);
        overlapped_.hEvent = completed_;
        if (!ReadDirectoryChangesW(directory_, buffer_.Data(), static_cast<DWORD>(kBufferBytes), FALSE, kNotifyFilter,
                                   nullptr, &overlapped_, nullptr))
        {
            return Status::FromOs(static_cast<i32>(GetLastError()));
        }
        pending_ = true;
        return Status::Success();
    }

    // Takes the completed read, if any, parses it and arms the next one. The caller holds the mutex.
    void Drain()
    {
        while (pending_)
        {
            DWORD bytes = 0;
            if (GetOverlappedResult(directory_, &overlapped_, &bytes, FALSE))
            {
                pending_ = false;
                if (bytes == 0)
                {
                    queue_.Push(WatchEventKind::Overflow, StringView());  // more changes than the buffer held
                }
                else
                {
                    Parse(static_cast<usize>(bytes));
                }
            }
            else
            {
                const DWORD error = GetLastError();
                if (error == ERROR_IO_INCOMPLETE)
                {
                    // Nothing yet. A directory removed while watched may end no read: ask the handle.
                    if (DirectoryGone())
                    {
                        CancelRead();
                        EndWatch();
                    }
                    return;
                }
                pending_ = false;
                // ERROR_NOTIFY_ENUM_DIR: the buffer overflowed. ERROR_OPERATION_ABORTED: the thread that armed the read
                // ended, and the system cancelled its I/O (Close cancels only after taking pending_ down). Either way
                // events were lost: report an overflow and read again.
                if (error != ERROR_NOTIFY_ENUM_DIR && error != ERROR_OPERATION_ABORTED)
                {
                    EndWatch();
                    return;
                }
                queue_.Push(WatchEventKind::Overflow, StringView());
            }
            if (DirectoryGone() || Arm().Failed())
            {
                EndWatch();
                return;
            }
        }
    }

    // The watched directory was removed (or the read failed for good): report it once.
    void EndWatch()
    {
        if (!gone_reported_)
        {
            gone_reported_ = true;
            queue_.Push(WatchEventKind::Overflow, StringView());
        }
    }

    // A directory removed while open stays until its last handle closes, marked for deletion: the read then reports
    // its last changes and nothing more.
    bool DirectoryGone() const
    {
        FILE_STANDARD_INFO standard{};
        if (!GetFileInformationByHandleEx(directory_, FileStandardInfo, &standard, sizeof(standard)))
        {
            return true;
        }
        return standard.DeletePending != FALSE;
    }

    // Turns the records of one completed read into events. A rename is two records in a row (the old name, then the
    // new); an old name alone is a move out of the directory, a new name alone a move in.
    void Parse(usize bytes)
    {
        const u8* base = reinterpret_cast<const u8*>(buffer_.Data());
        String<HeapAllocator> from(HeapAllocator::Default());
        bool have_from = false;
        usize at = 0;
        for (;;)
        {
            if (at + sizeof(FILE_NOTIFY_INFORMATION) > bytes)
            {
                break;
            }
            const FILE_NOTIFY_INFORMATION* record = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(base + at);
            const usize name_bytes = record->FileNameLength;
            const usize header_bytes = static_cast<usize>(reinterpret_cast<const u8*>(record->FileName) -
                                                          reinterpret_cast<const u8*>(record));
            if (header_bytes + name_bytes > bytes - at)
            {
                break;  // a record that does not fit: never read past the data
            }
            // A component is at most 255 UTF-16 units, 3 bytes each in UTF-8; a longer name (none exists) is skipped.
            Array<char, 4 * MAX_PATH> name_text;
            const usize length = os::windows::Utf16ToUtf8(record->FileName, name_bytes / sizeof(wchar_t),
                                                          name_text.Data(), name_text.Size());
            const StringView name(name_text.Data(), length < name_text.Size() ? length : 0);
            if (have_from && record->Action != FILE_ACTION_RENAMED_NEW_NAME)
            {
                queue_.Push(WatchEventKind::Removed, from.View());  // a move out of the directory
                have_from = false;
            }
            if (name.Length() != 0 && !name.Contains(StringView("\\")) && !name.Contains(StringView("/")))
            {
                switch (record->Action)
                {
                case FILE_ACTION_ADDED: queue_.Push(WatchEventKind::Created, name); break;
                case FILE_ACTION_REMOVED: queue_.Push(WatchEventKind::Removed, name); break;
                case FILE_ACTION_MODIFIED: queue_.Push(WatchEventKind::Modified, name); break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                    from = String<HeapAllocator>(HeapAllocator::Default(), name);
                    have_from = true;
                    break;
                case FILE_ACTION_RENAMED_NEW_NAME:
                    if (have_from)
                    {
                        queue_.Push(WatchEventKind::Renamed, name, from.View());
                        have_from = false;
                    }
                    else
                    {
                        queue_.Push(WatchEventKind::Created, name);  // a move in from elsewhere
                    }
                    break;
                default: break;
                }
            }
            if (record->NextEntryOffset == 0)
            {
                break;
            }
            at += record->NextEntryOffset;
        }
        if (have_from)
        {
            queue_.Push(WatchEventKind::Removed, from.View());
        }
    }

    // Cancels the outstanding read, if any, and waits for it to end: after this the system no longer writes into the
    // buffer.
    void CancelRead()
    {
        if (pending_)
        {
            CancelIoEx(directory_, &overlapped_);
            DWORD bytes = 0;
            GetOverlappedResult(directory_, &overlapped_, &bytes, TRUE);
            pending_ = false;
        }
    }

    // Cancels the read, then closes the handles.
    void Close()
    {
        CancelRead();
        if (directory_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(directory_);
            directory_ = INVALID_HANDLE_VALUE;
        }
        if (completed_ != nullptr)
        {
            CloseHandle(completed_);
            completed_ = nullptr;
        }
        if (wake_ != nullptr)
        {
            CloseHandle(wake_);
            wake_ = nullptr;
        }
    }

    HANDLE directory_ = INVALID_HANDLE_VALUE;
    HANDLE completed_ = nullptr;
    HANDLE wake_ = nullptr;
    OVERLAPPED overlapped_{};
    Vector<u64> buffer_;
    bool pending_ = false;
    Mutex mutex_;
    detail::WatchQueue queue_;
    bool gone_reported_ = false;
};

}  // namespace

Status OpenNativeWatcher(const Path& directory, UniquePtr<DirectoryWatcher>* out)
{
    UniquePtr<ReadDirectoryChangesWatcher> watcher = MakeUnique<ReadDirectoryChangesWatcher>();
    const Status started = watcher->Start(directory);
    if (started.Failed())
    {
        return started;
    }
    *out = UniquePtr<DirectoryWatcher>(watcher.Release());
    return Status::Success();
}

}  // namespace vse::io
