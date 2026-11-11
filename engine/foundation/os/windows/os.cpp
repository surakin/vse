#include "os/os.h"

#include "os/console.h"
#include "sync/atomic.h"
#include <fcntl.h>  // the POSIX-named open flags callers pass (_O_RDONLY, ...), from the UCRT

#include "os/windows/utf16.h"
#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <bcrypt.h>  // BCryptGenRandom (bcrypt.lib)
#include <psapi.h>   // GetProcessMemoryInfo (K32GetProcessMemoryInfo in kernel32, PSAPI_VERSION 2)
#include "unity/windows_disable.h"

// The Windows half of os/os.h (Phase 13 Day 149; spec docs/superpowers/specs/2026-11-07-windows-port-design.md,
// decision 3).
//
// Descriptors are an engine table of HANDLEs behind the `int` the interface keeps: 0, 1 and 2 are the standard handles,
// looked up when used; 3 and up are slots of a fixed table claimed with a compare-and-swap. Nothing here takes a lock
// or allocates, so the crash report (which creates its file and writes it from inside the crash hook) can use every
// descriptor function; the UCRT's own descriptors were rejected because a thread suspended inside _write holds a
// per-descriptor lock that a crash path then waits on forever (Spike A).

namespace engine_placeholder::os
{

namespace
{

constexpr int kFirstTableDescriptor = 3;
constexpr int kTableSize = 256;
Atomic<HANDLE> g_handles[kTableSize] = {};

constexpr DWORD kMaxIoPerCall = 0x7FFFFFFF;  // `long` is 32 bits on Windows: one call moves at most this much and
                                             // reports a short count

HANDLE HandleOf(int fd)
{
    switch (fd)
    {
    case 0: return GetStdHandle(STD_INPUT_HANDLE);
    case 1: return GetStdHandle(STD_OUTPUT_HANDLE);
    case 2: return GetStdHandle(STD_ERROR_HANDLE);
    default: break;
    }
    if (fd < kFirstTableDescriptor || fd >= kFirstTableDescriptor + kTableSize)
    {
        return INVALID_HANDLE_VALUE;
    }
    HANDLE h = g_handles[fd - kFirstTableDescriptor].Load(MemoryOrder::Acquire);
    return h != nullptr ? h : INVALID_HANDLE_VALUE;
}

int Adopt(HANDLE h)
{
    if (h == INVALID_HANDLE_VALUE || h == nullptr)
    {
        return -1;
    }
    for (int i = 0; i < kTableSize; ++i)
    {
        HANDLE expected = nullptr;
        if (g_handles[i].CompareExchangeStrong(expected, h, MemoryOrder::AcqRel))
        {
            return i + kFirstTableDescriptor;
        }
    }
    CloseHandle(h);  // the table is full
    return -1;
}

bool QueryIsConsole(HANDLE h)
{
    DWORD mode = 0;
    return GetFileType(h) == FILE_TYPE_CHAR && GetConsoleMode(h, &mode) != 0;
}

// Whether a handle is a console, remembered for the last handle seen on each standard descriptor (a program rarely
// swaps them with SetStdHandle, and a changed handle is simply queried again) so a log line does not cost two system
// calls. Table slots are files and pipes from CreateFileW, queried each time (rare).
Atomic<HANDLE> g_std_seen[3] = {};
Atomic<bool> g_std_console[3] = {};

bool IsConsole(int fd, HANDLE h)
{
    if (fd < 0 || fd > 2)
    {
        return QueryIsConsole(h);
    }
    if (g_std_seen[fd].Load(MemoryOrder::Acquire) == h)
    {
        return g_std_console[fd].Load(MemoryOrder::Relaxed);
    }
    const bool console = QueryIsConsole(h);
    g_std_console[fd].Store(console, MemoryOrder::Relaxed);
    g_std_seen[fd].Store(h, MemoryOrder::Release);
    return console;
}

// Console output goes through WriteConsoleW, converted from UTF-8 in stack-sized chunks, instead of switching the
// console's code page to UTF-8 (SetConsoleOutputCP changes the console the user's shell shares, and the change outlives
// the program). Returns the UTF-8 bytes consumed. A multi-byte sequence split across two calls of Write is shown as
// U+FFFD: callers write whole lines.
long WriteConsoleUtf8(HANDLE h, const char* bytes, usize len)
{
    wchar_t wide[512];
    usize done = 0;
    while (done < len)
    {
        // Take up to 255 bytes, backing off to a code point boundary so a chunk never splits a sequence.
        usize take = len - done < 255 ? len - done : 255;
        while (take > 0 && done + take < len && (static_cast<u8>(bytes[done + take]) & 0xC0) == 0x80)
        {
            --take;
        }
        if (take == 0)
        {
            take = len - done < 255 ? len - done : 255;
        }
        const usize units = windows::Utf8ToUtf16(StringView(bytes + done, take), wide, sizeof(wide) / sizeof(wide[0]));
        DWORD written = 0;
        if (!WriteConsoleW(h, wide, static_cast<DWORD>(units), &written, nullptr))
        {
            return done > 0 ? static_cast<long>(done) : -1;
        }
        done += take;
    }
    return static_cast<long>(done);
}

// The UTF-8 path as a terminated UTF-16 path for CreateFileW, refusing what does not fit instead of truncating.
bool WidePath(StringView path, wchar_t (&wide)[kMaxPathBytes])
{
    if (path.Length() >= kMaxPathBytes)
    {
        return false;
    }
    const usize n = windows::PathToWin32(path, wide, kMaxPathBytes);
    return n < kMaxPathBytes;
}

}  // namespace

void Exit(int code)
{
    ::exit(code);  // as on Linux: atexit handlers and static destructors run, the C runtime flushes
}

long Write(int fd, const void* buf, usize len)
{
    HANDLE h = HandleOf(fd);
    if (h == INVALID_HANDLE_VALUE || h == nullptr)
    {
        return -1;
    }
    if (IsConsole(fd, h))
    {
        return WriteConsoleUtf8(h, static_cast<const char*>(buf), len);
    }
    const DWORD chunk = len > kMaxIoPerCall ? kMaxIoPerCall : static_cast<DWORD>(len);
    DWORD written = 0;
    if (!WriteFile(h, buf, chunk, &written, nullptr))
    {
        return -1;
    }
    return static_cast<long>(written);
}

namespace
{

// WriteAllWithTimeout's writer. A blocking write to a pipe whose reader stopped, or to a console the user paused (a
// QuickEdit selection, Ctrl+S), never returns, and Windows offers no way to bound one without changing the handle for
// every other thread (PIPE_NOWAIT). So the write runs on a helper thread from a static staging buffer and the caller
// waits only until the deadline; a write that is still blocked then is left to finish (or be killed with the process)
// and keeps the buffer, and later calls give up at once while it is busy. No heap, no lock: the crash report calls this
// from the crash hook.
struct TimedWrite
{
    int fd;
    HANDLE handle;
    DWORD length;
    long result;
};
char g_stage[64 * 1024];
TimedWrite g_timed_write;
Atomic<bool> g_stage_busy{false};

DWORD WINAPI TimedWriteThread(void*)
{
    const TimedWrite& w = g_timed_write;
    long n = 0;
    if (IsConsole(w.fd, w.handle))
    {
        n = WriteConsoleUtf8(w.handle, g_stage, w.length);
    }
    else
    {
        DWORD written = 0;
        n = WriteFile(w.handle, g_stage, w.length, &written, nullptr) ? static_cast<long>(written) : -1;
    }
    g_timed_write.result = n;
    g_stage_busy.Store(false, MemoryOrder::Release);  // the buffer is free again, whether or not the caller still waits
    return 0;
}

}  // namespace

long WriteAllWithTimeout(int fd, const void* buf, usize len, u64 timeout_nanos)
{
    HANDLE h = HandleOf(fd);
    if (h == INVALID_HANDLE_VALUE || h == nullptr)
    {
        return 0;
    }
    const char* bytes = static_cast<const char*>(buf);
    usize done = 0;
    const u64 deadline = MonotonicTimeNanos() + timeout_nanos;
    while (done < len)
    {
        bool expected = false;
        if (!g_stage_busy.CompareExchangeStrong(expected, true, MemoryOrder::AcqRel))
        {
            break;  // an earlier write is still blocked
        }
        const usize chunk = len - done < sizeof(g_stage) ? len - done : sizeof(g_stage);
        for (usize i = 0; i < chunk; ++i)
        {
            g_stage[i] = bytes[done + i];
        }
        g_timed_write = TimedWrite{fd, h, static_cast<DWORD>(chunk), -1};
        HANDLE thread = CreateThread(nullptr, 64 * 1024, TimedWriteThread, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION,
                                     nullptr);
        if (thread == nullptr)
        {
            g_stage_busy.Store(false, MemoryOrder::Release);
            break;
        }
        const u64 now = MonotonicTimeNanos();
        const DWORD wait_ms = deadline > now ? static_cast<DWORD>((deadline - now + 999'999) / 1'000'000) : 0;
        const DWORD waited = WaitForSingleObject(thread, wait_ms);
        CloseHandle(thread);  // the thread runs on (if it must) without its handle
        if (waited != WAIT_OBJECT_0)
        {
            break;  // blocked past the deadline: what is written so far is the answer
        }
        const long n = g_timed_write.result;
        if (n <= 0)
        {
            break;
        }
        done += static_cast<usize>(n);
    }
    return static_cast<long>(done);
}

long Read(int fd, void* buf, usize len)
{
    HANDLE h = HandleOf(fd);
    if (h == INVALID_HANDLE_VALUE || h == nullptr)
    {
        return -1;
    }
    if (IsConsole(fd, h))
    {
        // A console delivers text in its input code page through ReadFile; read UTF-16 instead and hand out UTF-8, as
        // Write takes it. At most len / 4 units are read, so the UTF-8 always fits (a unit is at most 3 bytes, a
        // surrogate pair 4 for two units).
        wchar_t wide[256];
        usize want = len / 4;
        if (want == 0)
        {
            return len == 0 ? 0 : -1;  // a buffer under 4 bytes cannot be guaranteed to hold one character
        }
        if (want > 256)
        {
            want = 256;
        }
        DWORD got = 0;
        if (!ReadConsoleW(h, wide, static_cast<DWORD>(want), &got, nullptr))
        {
            return -1;
        }
        return static_cast<long>(windows::Utf16ToUtf8(wide, got, static_cast<char*>(buf), len));
    }
    const DWORD chunk = len > kMaxIoPerCall ? kMaxIoPerCall : static_cast<DWORD>(len);
    DWORD got = 0;
    if (!ReadFile(h, buf, chunk, &got, nullptr))
    {
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;  // the writer closed its end: end of file, as on Linux
    }
    return static_cast<long>(got);
}

int Open(StringView path, int flags, int mode)
{
    wchar_t wide[kMaxPathBytes];
    if (!WidePath(path, wide))
    {
        return -1;
    }
    DWORD access = 0;
    switch (flags & (_O_RDONLY | _O_WRONLY | _O_RDWR))
    {
    case _O_WRONLY: access = GENERIC_WRITE; break;
    case _O_RDWR: access = GENERIC_READ | GENERIC_WRITE; break;
    default: access = GENERIC_READ; break;
    }
    if (flags & _O_APPEND)
    {
        access = (access & ~static_cast<DWORD>(GENERIC_WRITE)) | FILE_APPEND_DATA;
    }
    DWORD disposition = OPEN_EXISTING;
    if ((flags & _O_CREAT) && (flags & _O_EXCL))
    {
        disposition = CREATE_NEW;
    }
    else if ((flags & _O_CREAT) && (flags & _O_TRUNC))
    {
        disposition = CREATE_ALWAYS;
    }
    else if (flags & _O_CREAT)
    {
        disposition = OPEN_ALWAYS;
    }
    else if (flags & _O_TRUNC)
    {
        disposition = TRUNCATE_EXISTING;
    }
    // POSIX permission bits have one Windows counterpart: no write bit for anyone makes a new file read-only. A private
    // file (0600) is private through the user's profile directories' ACLs, not through this call.
    const DWORD attributes = (flags & _O_CREAT) && (mode & 0222) == 0 ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL;
    HANDLE h = CreateFileW(wide, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, disposition,
                           attributes, nullptr);
    return Adopt(h);
}

int CreateFileForWriting(StringView path, int mode)
{
    return Open(path, _O_WRONLY | _O_CREAT | _O_TRUNC, mode);
}

int Close(int fd)
{
    if (fd >= 0 && fd < kFirstTableDescriptor)
    {
        return 0;  // the standard handles belong to the process
    }
    if (fd < kFirstTableDescriptor || fd >= kFirstTableDescriptor + kTableSize)
    {
        return -1;
    }
    HANDLE h = g_handles[fd - kFirstTableDescriptor].Exchange(nullptr, MemoryOrder::AcqRel);
    if (h == nullptr)
    {
        return -1;
    }
    return CloseHandle(h) ? 0 : -1;
}

void* ReserveMemory(usize size)
{
    return VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);  // committed pages read as zero
}

void ReleaseMemory(void* addr, usize size)
{
    (void)size;  // MEM_RELEASE frees the whole reservation VirtualAlloc made; it takes no size
    if (addr != nullptr)
    {
        VirtualFree(addr, 0, MEM_RELEASE);
    }
}

i64 ResidentMemoryKiB()
{
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
    {
        return 0;
    }
    return static_cast<i64>(counters.WorkingSetSize / 1024);
}

i64 OpenHandleCount()
{
    DWORD count = 0;
    return GetProcessHandleCount(GetCurrentProcess(), &count) ? static_cast<i64>(count) : 0;
}

u64 MonotonicTimeNanos()
{
    static const u64 frequency = []
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);  // fixed at boot; never fails on Windows XP and later
        return static_cast<u64>(f.QuadPart);
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const u64 ticks = static_cast<u64>(now.QuadPart);
    // Whole seconds and the remainder separately, so `ticks * 1e9` cannot overflow.
    return (ticks / frequency) * 1'000'000'000ULL + (ticks % frequency) * 1'000'000'000ULL / frequency;
}

u64 WallClockUnixMillis()
{
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    const u64 ticks = (static_cast<u64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;  // 100 ns since 1601-01-01
    constexpr u64 kTicksAtUnixEpoch = 116444736000000000ULL;
    return (ticks - kTicksAtUnixEpoch) / 10'000ULL;
}

void FillRandomBytes(void* buffer, usize count)
{
    u8* out = static_cast<u8*>(buffer);
    while (count > 0)
    {
        const ULONG chunk = count > 0x40000000u ? 0x40000000u : static_cast<ULONG>(count);
        if (BCryptGenRandom(nullptr, out, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        {
            Abort();
        }
        out += chunk;
        count -= chunk;
    }
}

void SleepNanos(u64 nanos)
{
    if (nanos == 0)
    {
        return;
    }
    const u64 deadline = MonotonicTimeNanos() + nanos;
    // One high-resolution waitable timer per thread (Windows 10 1803 and later); Sleep() has millisecond steps on top
    // of the timer tick.
    struct ThreadTimer
    {
        HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                               TIMER_ALL_ACCESS);
        ~ThreadTimer()
        {
            if (handle != nullptr)
            {
                CloseHandle(handle);
            }
        }
    };
    thread_local ThreadTimer timer;
    for (;;)
    {
        const u64 now = MonotonicTimeNanos();
        if (now >= deadline)
        {
            return;
        }
        const u64 left = deadline - now;
        if (timer.handle != nullptr)
        {
            LARGE_INTEGER due;
            due.QuadPart = -static_cast<LONGLONG>((left + 99) / 100);  // relative, in 100 ns units, rounded up
            if (SetWaitableTimerEx(timer.handle, &due, 0, nullptr, nullptr, nullptr, 0))
            {
                WaitForSingleObject(timer.handle, INFINITE);
                continue;
            }
        }
        Sleep(static_cast<DWORD>((left + 999'999) /
                                 1'000'000));  // no high-resolution timer: whole milliseconds, rounded up
    }
}

DynamicLibraryHandle LoadLibrary(StringView path)
{
    wchar_t wide[kMaxPathBytes];
    if (!WidePath(path, wide))
    {
        return nullptr;
    }
    // Never the current directory. A bare name ("vulkan-1.dll") is looked for in the program's directory, System32 and
    // the directories added with AddDllDirectory; an absolute path is loaded as named (with its own directory searched
    // for its dependencies). A relative path with a directory in it is refused: its meaning would depend on the current
    // directory, which the search order exists to exclude.
    bool has_dir = false;
    for (const wchar_t* c = wide; *c != 0; ++c)
    {
        has_dir = has_dir || *c == L'\\';
    }
    const bool absolute = (wide[0] != 0 && wide[1] == L':' && wide[2] == L'\\') ||
                          (wide[0] == L'\\' && wide[1] == L'\\');
    if (has_dir && !absolute)
    {
        return nullptr;
    }
    const DWORD flags = absolute ? LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
                                 : LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
    return LoadLibraryExW(wide, nullptr, flags);
}

void* GetSymbol(DynamicLibraryHandle handle, const char* symbol_name)
{
    if (handle == nullptr)
    {
        return nullptr;
    }
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), symbol_name));
}

void UnloadLibrary(DynamicLibraryHandle handle)
{
    if (handle != nullptr)
    {
        FreeLibrary(static_cast<HMODULE>(handle));
    }
}

namespace
{

// Calls `visit(entry, length)` for each environment entry of the process, skipping the `=C:=C:\dir`-style entries
// Windows keeps for each drive's current directory (they are not variables). Stops when `visit` returns false.
template <typename Visit> void ForEachEnvironmentEntry(Visit visit)
{
    wchar_t* block = GetEnvironmentStringsW();
    if (block == nullptr)
    {
        return;
    }
    for (const wchar_t* entry = block; *entry != 0;)
    {
        usize n = 0;
        while (entry[n] != 0)
        {
            ++n;
        }
        if (entry[0] != L'=' && !visit(entry, n))
        {
            break;
        }
        entry += n + 1;
    }
    FreeEnvironmentStringsW(block);
}

}  // namespace

bool EnvironmentNamesIgnoreCase()
{
    return true;
}  // GetEnvironmentVariable("path") finds PATH

usize EnvironmentCount()
{
    usize count = 0;
    ForEachEnvironmentEntry(
        [&](const wchar_t*, usize)
        {
            ++count;
            return true;
        });
    return count;
}

bool EnvironmentEntry(usize index, char* buffer, usize capacity, usize* length)
{
    *length = 0;
    if (capacity > 0)
    {
        buffer[0] = '\0';
    }
    bool found = false;
    usize i = 0;
    ForEachEnvironmentEntry(
        [&](const wchar_t* entry, usize n)
        {
            if (i++ != index)
            {
                return true;
            }
            *length = windows::Utf16ToUtf8(entry, n, buffer, capacity);
            found = true;
            return false;
        });
    return found;
}

}  // namespace engine_placeholder::os
