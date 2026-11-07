// Death tests on Windows (Phase 13 Day 155; os/windows/debug/BACKEND.md, DeathTestsSupported and RunInChild): the test program starts itself
// again with CreateProcessW, suspended, inside a job object that kills the child (and anything it starts) when the job handle closes, which
// also happens if the parent dies, and that suppresses the error dialogs; then it reads the child's standard output and error until it ends
// or the deadline passes. The child protocol (environment variables naming the case) is the Linux one.
#include "debug/death_test.h"
#include "debug/death_test_platform.h"

#include "containers/vector.h"
#include "os/os.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace engine_placeholder::debug {

namespace {

constexpr const char* kChildNameVariable = "ENGINE_DEATH_TEST_CHILD";
constexpr const char* kChildArgumentVariable = "ENGINE_DEATH_TEST_ARG";
constexpr const char* kNoCoreVariable = "ENGINE_DEATH_TEST_NOCORE";
constexpr u32 kDefaultTimeoutMs = 10000;
constexpr DWORD kPollSliceMs = 10;
constexpr usize kDrainBudgetBytes = 256 * 1024;
constexpr UINT kTimeoutExitCode = 0xDEAD0001u; // the exit code TerminateJobObject gives a child that ran out of time
constexpr const char kAbortMarker[] = "ENGINE-ABORT";

// abort() ends a process with exit code 3, as exit(3) does: the child's SIGABRT handler writes this marker first, so the parent can tell them
// apart ("exit code 3 and the marker" is Abort).
void OnAbortSignal(int) {
    static const char kLine[] = "ENGINE-ABORT\n";
    os::Write(2, kLine, sizeof(kLine) - 1);
    std::_Exit(3);
}

// Sanitizer flags that make a child's faults reach its exit code instead of being reported (and turned into an exit code) by the runtime.
constexpr const char* kAsanFaultFlags = "handle_segv=0:handle_sigill=0:handle_sigfpe=0:handle_abort=0:detect_leaks=0";

CrashReason ReasonForStatus(DWORD code) {
    switch (code) {
    case 0xC0000005u: return CrashReason::AccessViolation;    // STATUS_ACCESS_VIOLATION
    case 0xC0000006u: return CrashReason::BusError;           // STATUS_IN_PAGE_ERROR
    case 0xC000001Du:                                         // STATUS_ILLEGAL_INSTRUCTION
    case 0xC0000096u: return CrashReason::IllegalInstruction; // STATUS_PRIVILEGED_INSTRUCTION
    case 0xC0000094u:                                         // STATUS_INTEGER_DIVIDE_BY_ZERO
    case 0xC0000095u:                                         // STATUS_INTEGER_OVERFLOW
    case 0xC000008Eu:                                         // STATUS_FLOAT_DIVIDE_BY_ZERO
    case 0xC0000090u: return CrashReason::ArithmeticFault;    // STATUS_FLOAT_INVALID_OPERATION
    case 0xC00000FDu: return CrashReason::StackOverflow;      // STATUS_STACK_OVERFLOW
    case 0x40000015u: return CrashReason::Abort;              // STATUS_FATAL_APP_EXIT (sentry's abort)
    default: return CrashReason::None;
    }
}

// An exception status (severity bits 11, or the warning/breakpoint codes), not a program's own exit code.
bool IsExceptionStatus(DWORD code) { return (code & 0xC0000000u) == 0xC0000000u || code == 0x80000003u || code == 0x40000015u; }

bool KeyEquals(const wchar_t* entry, const char* key) {
    usize i = 0;
    for (; key[i] != '\0'; ++i) {
        wchar_t a = entry[i];
        wchar_t b = static_cast<wchar_t>(key[i]);
        if (a >= L'a' && a <= L'z') a = static_cast<wchar_t>(a - L'a' + L'A'); // environment names are case-insensitive on Windows
        if (b >= L'a' && b <= L'z') b = static_cast<wchar_t>(b - L'a' + L'A');
        if (a != b) return false;
    }
    return entry[i] == L'=';
}

// The key of `extra` (`NAME=value`) as a NUL-terminated copy in `out`.
void KeyOf(const char* extra, char* out, usize capacity) {
    usize i = 0;
    while (extra[i] != '\0' && extra[i] != '=' && i + 1 < capacity) {
        out[i] = extra[i];
        ++i;
    }
    out[i] = '\0';
}

bool IsSanitizerKey(const wchar_t* entry) {
    return KeyEquals(entry, "ASAN_OPTIONS") || KeyEquals(entry, "UBSAN_OPTIONS") || KeyEquals(entry, "LSAN_OPTIONS");
}

void AppendWide(Vector<wchar_t>& out, const wchar_t* text) {
    for (; *text != 0; ++text) out.Push(*text);
}

void AppendUtf8(Vector<wchar_t>& out, StringView text) {
    wchar_t buffer[4096];
    const usize n = os::windows::Utf8ToUtf16(text, buffer, sizeof(buffer) / sizeof(buffer[0]));
    const usize kept = n < sizeof(buffer) / sizeof(buffer[0]) ? n : sizeof(buffer) / sizeof(buffer[0]) - 1;
    for (usize i = 0; i < kept; ++i) out.Push(buffer[i]);
}

// One argument quoted by the rules CommandLineToArgvW and the C runtime read back (backslashes double only before a quote).
void AppendQuoted(Vector<wchar_t>& out, const wchar_t* arg) {
    out.Push(L'"');
    usize backslashes = 0;
    for (const wchar_t* p = arg;; ++p) {
        if (*p == L'\\') {
            ++backslashes;
            continue;
        }
        if (*p == 0) {
            for (usize i = 0; i < backslashes * 2; ++i) out.Push(L'\\'); // before the closing quote
            break;
        }
        if (*p == L'"') {
            for (usize i = 0; i < backslashes * 2 + 1; ++i) out.Push(L'\\');
        } else {
            for (usize i = 0; i < backslashes; ++i) out.Push(L'\\');
        }
        backslashes = 0;
        out.Push(*p);
    }
    out.Push(L'"');
}

// The child's environment: the parent's block (minus our variables, the caller's replacements and, when faults must reach the exit code, the
// sanitizer options), the caller's extras, then ours. A wide block: `NAME=value\0...\0\0`.
bool BuildEnvironment(Vector<wchar_t>& env, const char* name, const char* argument, const ChildOptions& options) {
    wchar_t* parent = GetEnvironmentStringsW();
    if (parent == nullptr) return false;
    for (const wchar_t* e = parent; *e != 0; e += std::wcslen(e) + 1) {
        if (KeyEquals(e, kChildNameVariable) || KeyEquals(e, kChildArgumentVariable) || KeyEquals(e, kNoCoreVariable)) continue;
        bool replaced = options.sanitizers_pass_faults && IsSanitizerKey(e);
        for (usize i = 0; i < options.extra_env_count && !replaced; ++i) {
            char key[128];
            KeyOf(options.extra_env[i], key, sizeof(key));
            replaced = KeyEquals(e, key);
        }
        if (replaced) continue;
        AppendWide(env, e);
        env.Push(0);
    }
    FreeEnvironmentStringsW(parent);
    for (usize i = 0; i < options.extra_env_count; ++i) {
        // A sanitizer option the caller passes is merged below instead of added twice (the child would read only the first).
        if (options.sanitizers_pass_faults && std::strncmp(options.extra_env[i], "ASAN_OPTIONS=", 13) == 0) continue;
        AppendUtf8(env, StringView(options.extra_env[i]));
        env.Push(0);
    }
    AppendUtf8(env, StringView("ENGINE_DEATH_TEST_CHILD="));
    AppendUtf8(env, StringView(name));
    env.Push(0);
    AppendUtf8(env, StringView("ENGINE_DEATH_TEST_ARG="));
    AppendUtf8(env, StringView(argument));
    env.Push(0);
    if (options.disable_core_dumps) {
        AppendUtf8(env, StringView("ENGINE_DEATH_TEST_NOCORE=1"));
        env.Push(0);
    }
    if (options.sanitizers_pass_faults) {
        // The caller's value wins over the parent's own; either way the fault flags are appended after it.
        const char* existing = nullptr;
        for (usize i = 0; i < options.extra_env_count; ++i) {
            if (std::strncmp(options.extra_env[i], "ASAN_OPTIONS=", 13) == 0) existing = options.extra_env[i] + 13;
        }
        if (existing == nullptr) existing = std::getenv("ASAN_OPTIONS");
        AppendUtf8(env, StringView("ASAN_OPTIONS="));
        if (existing != nullptr && existing[0] != '\0') {
            AppendUtf8(env, StringView(existing));
            AppendUtf8(env, StringView(":"));
        }
        AppendUtf8(env, StringView(kAsanFaultFlags));
        env.Push(0);
    }
    env.Push(0);
    return true;
}

void Append(char* buffer, usize& length, bool& truncated, const char* data, usize count) {
    const usize room = kChildCaptureBytes - length;
    const usize take = count < room ? count : room;
    std::memcpy(buffer + length, data, take);
    length += take;
    if (take < count) truncated = true;
}

// Reads what the pipe holds now, up to kDrainBudgetBytes, without blocking; closes and clears `pipe` at its end (all writers gone).
void DrainPipe(HANDLE& pipe, char* buffer, usize& length, bool& truncated) {
    if (pipe == nullptr) return;
    usize budget = kDrainBudgetBytes;
    while (budget > 0) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
            CloseHandle(pipe); // ERROR_BROKEN_PIPE: every write end is closed and the pipe is empty
            pipe = nullptr;
            return;
        }
        if (available == 0) return;
        char chunk[4096];
        DWORD got = 0;
        if (!ReadFile(pipe, chunk, available < sizeof(chunk) ? available : static_cast<DWORD>(sizeof(chunk)), &got, nullptr) || got == 0) {
            CloseHandle(pipe);
            pipe = nullptr;
            return;
        }
        Append(buffer, length, truncated, chunk, got);
        budget = got < budget ? budget - got : 0;
    }
}

// Everything a spawn opens, released in the destructor on every path.
struct SpawnResources {
    HANDLE out_read = nullptr, out_write = nullptr;
    HANDLE err_read = nullptr, err_write = nullptr;
    HANDLE null_device = INVALID_HANDLE_VALUE;
    HANDLE job = nullptr;
    PROCESS_INFORMATION process{};
    Vector<u64> attribute_storage; // the PROC_THREAD_ATTRIBUTE_LIST, 8-byte aligned
    bool attributes_ready = false;

    ~SpawnResources() {
        for (HANDLE* h : {&out_read, &out_write, &err_read, &err_write, &process.hThread, &process.hProcess}) {
            if (*h != nullptr) CloseHandle(*h);
            *h = nullptr;
        }
        if (null_device != INVALID_HANDLE_VALUE) CloseHandle(null_device);
        if (attributes_ready) DeleteProcThreadAttributeList(reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.Data()));
        if (job != nullptr) CloseHandle(job); // kills whatever is still in the job
    }
};

// An anonymous pipe whose write end the child inherits and whose read end it does not.
bool MakePipe(HANDLE* read, HANDLE* write) {
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    if (!CreatePipe(read, write, &inherit, 64 * 1024)) return false;
    return SetHandleInformation(*read, HANDLE_FLAG_INHERIT, 0) != FALSE;
}

} // namespace

namespace detail {

bool PlatformCanSpawn() { return true; }

bool PlatformChildRequest(const char** name, const char** argument) {
    const char* requested = std::getenv(kChildNameVariable);
    if (requested == nullptr) return false;
    const char* arg = std::getenv(kChildArgumentVariable);
    *name = requested;
    *argument = arg != nullptr ? arg : "";
    return true;
}

void PlatformPrepareChild() {
    // No error dialog for a crash or a missing disk, and no "abort() has been called" box or WER report from the C runtime: the parent reads the
    // exit code. (The job object also sets SEM_NOGPFAULTERRORBOX for the child.)
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::signal(SIGABRT, &OnAbortSignal);
}

ChildResult PlatformRunChild(const char* name, const char* argument, const ChildOptions& options) {
    ChildResult result;
    result.supported = true;

    static const char* const kDefaultArguments[] = {"[.death-child]", "--colour-mode", "none"};
    const char* const* extra_arguments = options.arguments != nullptr ? options.arguments : kDefaultArguments;
    const usize extra_argument_count = options.arguments != nullptr ? options.argument_count : 3;

    // The executable runs itself again; the command line is its quoted path and the arguments.
    wchar_t exe[MAX_PATH * 4];
    const DWORD exe_length = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(sizeof(exe) / sizeof(exe[0])));
    if (exe_length == 0 || exe_length >= sizeof(exe) / sizeof(exe[0])) return result;
    Vector<wchar_t> command;
    AppendQuoted(command, exe);
    for (usize i = 0; i < extra_argument_count; ++i) {
        command.Push(L' ');
        Vector<wchar_t> wide;
        AppendUtf8(wide, StringView(extra_arguments[i]));
        wide.Push(0);
        AppendQuoted(command, wide.Data());
    }
    command.Push(0);

    Vector<wchar_t> env;
    if (!BuildEnvironment(env, name, argument, options)) return result;

    SpawnResources res;
    if (!MakePipe(&res.err_read, &res.err_write)) return result;
    if (options.capture_stdout && !MakePipe(&res.out_read, &res.out_write)) return result;
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    res.null_device = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
    if (res.null_device == INVALID_HANDLE_VALUE) return result;

    // Only these handles are inherited (not every inheritable handle the test process happens to have open).
    HANDLE inherited[3] = {res.null_device, res.err_write, res.out_write};
    const DWORD inherited_count = options.capture_stdout ? 3 : 2;
    SIZE_T attribute_bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
    if (!res.attribute_storage.TryResize((attribute_bytes + 7) / 8)) return result;
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(res.attribute_storage.Data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_bytes)) return result;
    res.attributes_ready = true;
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, inherited_count * sizeof(HANDLE), nullptr, nullptr)) {
        return result;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = res.null_device;
    startup.StartupInfo.hStdOutput = options.capture_stdout ? res.out_write : res.null_device;
    startup.StartupInfo.hStdError = res.err_write;
    startup.lpAttributeList = attributes;

    // The job: the child and everything it starts die with it (the handle closes when the parent ends, too), and an unhandled exception ends
    // the process instead of showing a dialog.
    res.job = CreateJobObjectW(nullptr, nullptr);
    if (res.job == nullptr) return result;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    if (!SetInformationJobObject(res.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return result;

    if (!CreateProcessW(exe, command.Data(), nullptr, nullptr, TRUE,
                        CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, env.Data(), nullptr,
                        &startup.StartupInfo, &res.process)) {
        return result;
    }
    if (!AssignProcessToJobObject(res.job, res.process.hProcess)) {
        TerminateProcess(res.process.hProcess, kTimeoutExitCode);
        WaitForSingleObject(res.process.hProcess, INFINITE);
        return result;
    }
    ResumeThread(res.process.hThread);
    result.started = true;
    // The parent keeps only the read ends; closing its write ends is what lets the pipes report their end.
    CloseHandle(res.err_write);
    res.err_write = nullptr;
    if (res.out_write != nullptr) {
        CloseHandle(res.out_write);
        res.out_write = nullptr;
    }

    const u32 timeout_ms = options.timeout_ms != 0 ? options.timeout_ms : kDefaultTimeoutMs;
    // Nanoseconds throughout: a deadline truncated to milliseconds would fire up to 1 ms early.
    const u64 deadline = os::MonotonicTimeNanos() + static_cast<u64>(timeout_ms) * 1000000ull;
    for (;;) {
        DrainPipe(res.out_read, result.stdout_text, result.stdout_length, result.stdout_truncated);
        DrainPipe(res.err_read, result.stderr_text, result.stderr_length, result.stderr_truncated);
        const u64 now = os::MonotonicTimeNanos();
        const u64 remaining_ms = deadline > now ? (deadline - now + 999999ull) / 1000000ull : 0;
        const DWORD slice = static_cast<DWORD>(remaining_ms < kPollSliceMs ? remaining_ms : kPollSliceMs);
        if (WaitForSingleObject(res.process.hProcess, slice) == WAIT_OBJECT_0) break;
        if (os::MonotonicTimeNanos() >= deadline) {
            result.timed_out = true;
            TerminateJobObject(res.job, kTimeoutExitCode); // the child and any process it started
            WaitForSingleObject(res.process.hProcess, 5000);
            break;
        }
    }
    // Whatever the child wrote before it ended is still in the pipes; a grandchild still holding a write end is gone with the job.
    TerminateJobObject(res.job, kTimeoutExitCode);
    for (int round = 0; round < 64 && (res.out_read != nullptr || res.err_read != nullptr); ++round) {
        DrainPipe(res.out_read, result.stdout_text, result.stdout_length, result.stdout_truncated);
        DrainPipe(res.err_read, result.stderr_text, result.stderr_length, result.stderr_truncated);
        if (res.out_read != nullptr || res.err_read != nullptr) Sleep(1);
    }

    DWORD code = 0;
    if (!GetExitCodeProcess(res.process.hProcess, &code) || code == STILL_ACTIVE) return result; // the end was never seen
    if (result.timed_out && code != kTimeoutExitCode) result.timed_out = false; // it finished by itself just before the kill
    if (code == kTimeoutExitCode) {
        result.terminated = true; // killed from outside: no reason
        result.native_code = static_cast<i64>(code);
    } else if (code == 3 && result.StderrContains(kAbortMarker)) {
        result.terminated = true;
        result.native_code = static_cast<i64>(code);
        result.reason = CrashReason::Abort;
    } else if (IsExceptionStatus(code)) {
        result.terminated = true;
        result.native_code = static_cast<i64>(code);
        result.reason = ReasonForStatus(code);
    } else {
        result.exited = true;
        result.exit_code = static_cast<int>(code);
    }
    return result;
}

} // namespace detail

// Ends at once, as _exit does on Linux: TerminateProcess runs no atexit handler, static destructor or DLL detach. std::_Exit is not enough on
// Windows: it goes through ExitProcess, whose loader shutdown hung a death-test child about once in a hundred runs under AddressSanitizer
// with the crash library loaded (seen on Day 156: the child waited in RtlExitUserProcess until its deadline). The C streams are flushed first
// (a child's printf output is what the parent reads).
void ExitChildProcess(int code) {
    std::fflush(nullptr);
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
    for (;;) Sleep(INFINITE); // TerminateProcess on this process does not return; this keeps [[noreturn]] true if it ever did
}

} // namespace engine_placeholder::debug
