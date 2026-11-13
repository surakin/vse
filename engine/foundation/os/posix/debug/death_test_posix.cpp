// The POSIX death-test backend, shared by Linux and macOS (Phase 21 Day 291: moved from os/linux/debug/death_test.cpp);
// each system supplies the hooks of os/posix/debug/posix_debug.h (the pipe, the program's path, the environment, dying
// with the parent).
#include "debug/death_test.h"
#include "debug/death_test_platform.h"
#include "os/posix/debug/posix_debug.h"

#include "containers/vector.h"
#include "log/assert.h"
#include "os/os.h"

#include "os/last_error.h"
#include "os/signal.h"
#include "os/console.h"
#include "cstr/cstr.h"
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cstr/mem_ops.h"
#include "cstr/number.h"

namespace engine_placeholder::debug
{

namespace
{

constexpr const char* kChildNameVariable = "ENGINE_DEATH_TEST_CHILD";
constexpr const char* kChildArgumentVariable = "ENGINE_DEATH_TEST_ARG";
constexpr const char* kNoCoreVariable = "ENGINE_DEATH_TEST_NOCORE";
constexpr const char* kParentVariable = "ENGINE_DEATH_TEST_PARENT";
constexpr const char* kWatchVariable = "ENGINE_DEATH_TEST_WATCH";  // macOS: the child finds its parent-watch pipe on
                                                                   // posix::kParentWatchFd
constexpr u32 kDefaultTimeoutMs = 10000;
constexpr u32 kPollSliceMs = 10;
constexpr usize kEnvStorageBytes = 16 * 1024;
// The most one drain call reads: a child that writes as fast as we read must not keep us from checking the deadline.
constexpr usize kDrainBudgetBytes = 256 * 1024;

// Sanitizer flags that make a fatal fault reach the exit status instead of being handled (and turned into an exit code)
// by the sanitizer runtime.
constexpr const char*
    kAsanFaultFlags = "handle_segv=0:handle_sigbus=0:handle_sigill=0:handle_sigfpe=0:handle_abort=0:detect_leaks=0";
constexpr const char* kOtherFaultFlags = "handle_segv=0:handle_sigbus=0:handle_sigill=0:handle_sigfpe=0:handle_abort=0";

CrashReason ReasonForSignal(int signal_number)
{
    switch (signal_number)
    {
    case SIGSEGV: return CrashReason::AccessViolation;
    case SIGBUS: return CrashReason::BusError;
    case SIGILL: return CrashReason::IllegalInstruction;
    case SIGFPE: return CrashReason::ArithmeticFault;
    case SIGABRT: return CrashReason::Abort;
    default: return CrashReason::None;
    }
}

// Builds the child's environment block: the parent's variables (minus the ones we set), the extras, then ours. The
// strings live in fixed storage, so building it allocates only the pointer array.
class EnvironmentBlock
{
public:
    bool Reserve(usize count) { return pointers_.TryReserve(count + 1); }

    // Adds one string made of up to four pieces joined together. Returns false when the storage is full.
    bool Add(const char* a, const char* b = "", const char* c = "", const char* d = "")
    {
        const char* const parts[4] = {a, b, c, d};
        usize total = 1;
        for (const char* part : parts)
        {
            total += CStrLength(part);
        }
        if (used_ + total > kEnvStorageBytes)
        {
            return false;
        }
        char* out = storage_ + used_;
        usize at = 0;
        for (const char* part : parts)
        {
            const usize n = CStrLength(part);
            MemCopy(out + at, part, n);
            at += n;
        }
        out[at] = '\0';
        used_ += total;
        pointers_.Push(out);
        return true;
    }

    // Adds an existing string by pointer (the parent's own environment entry).
    void AddBorrowed(char* entry) { pointers_.Push(entry); }

    char* const* Finish()
    {
        pointers_.Push(nullptr);
        return pointers_.Data();
    }

private:
    char storage_[kEnvStorageBytes];
    usize used_ = 0;
    Vector<char*> pointers_;
};

bool KeyMatches(const char* entry, const char* key)
{
    const usize n = CStrLength(key);
    return CStrCompareN(entry, key, n) == 0 && entry[n] == '=';
}

bool KeyOfExtraMatches(const char* entry, const char* extra)
{
    const char* eq = CStrFindChar(extra, '=');
    const usize n = eq == nullptr ? CStrLength(extra) : static_cast<usize>(eq - extra);
    return CStrCompareN(entry, extra, n) == 0 && entry[n] == '=';
}

// Everything a spawn opens, released in the destructor on every path.
struct SpawnResources
{
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    int watch_pipe[2] = {-1, -1};  // macOS: the child reads [0] as posix::kParentWatchFd; the parent keeps [1] open,
                                   // unwritten, until it has reaped
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attr;
    bool actions_ready = false;
    bool attr_ready = false;

    ~SpawnResources()
    {
        CloseFd(out_pipe[0]);
        CloseFd(out_pipe[1]);
        CloseFd(err_pipe[0]);
        CloseFd(err_pipe[1]);
        CloseFd(watch_pipe[0]);
        CloseFd(watch_pipe[1]);
        if (actions_ready)
        {
            posix_spawn_file_actions_destroy(&actions);
        }
        if (attr_ready)
        {
            posix_spawnattr_destroy(&attr);
        }
    }

    static void CloseFd(int& fd)
    {
        if (fd >= 0)
        {
            ::close(fd);
            fd = -1;
        }
    }
};

void Append(char* buffer, usize& length, bool& truncated, const char* data, usize count)
{
    const usize room = kChildCaptureBytes - length;
    const usize take = count < room ? count : room;
    MemCopy(buffer + length, data, take);
    length += take;
    if (take < count)
    {
        truncated = true;
    }
}

// Reads what is available from a non-blocking pipe, up to kDrainBudgetBytes, so a child that writes without pause
// cannot keep the caller from checking its deadline; clears `fd` at end of file.
void DrainPipe(int& fd, char* buffer, usize& length, bool& truncated)
{
    if (fd < 0)
    {
        return;
    }
    char chunk[4096];
    usize budget = kDrainBudgetBytes;
    while (budget > 0)
    {
        const ssize_t n = ::read(fd, chunk, sizeof(chunk));
        if (n > 0)
        {
            Append(buffer, length, truncated, chunk, static_cast<usize>(n));
            budget = static_cast<usize>(n) < budget ? budget - static_cast<usize>(n) : 0;
            continue;
        }
        if (n < 0 && os::LastError() == EINTR)
        {
            continue;
        }
        if (n < 0 && (os::LastError() == EAGAIN || os::LastError() == EWOULDBLOCK))
        {
            return;
        }
        fd = -1;  // end of file or a real error: stop polling this pipe (the owner closes it)
        return;
    }
}

bool PrepareSpawn(SpawnResources& res, const ChildOptions& options)
{
    if (!posix::MakeCloexecPipe(res.out_pipe) || !posix::MakeCloexecPipe(res.err_pipe))
    {
        return false;
    }
    if (posix::ChildNeedsParentWatch())
    {
        if (!posix::MakeCloexecPipe(res.watch_pipe))
        {
            return false;
        }
        // dup2 onto the same number keeps close-on-exec set, so the read end must not already be kParentWatchFd: move
        // it above it.
        if (res.watch_pipe[0] == posix::kParentWatchFd)
        {
            const int moved = ::fcntl(res.watch_pipe[0], F_DUPFD_CLOEXEC, posix::kParentWatchFd + 1);
            if (moved < 0)
            {
                return false;
            }
            ::close(res.watch_pipe[0]);
            res.watch_pipe[0] = moved;
        }
    }
    for (int fd : {res.out_pipe[0], res.err_pipe[0]})
    {
        const int flags = ::fcntl(fd, F_GETFL);
        if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
        {
            return false;
        }
    }
    if (posix_spawn_file_actions_init(&res.actions) != 0)
    {
        return false;
    }
    res.actions_ready = true;
    if (options.capture_stdout)
    {
        if (posix_spawn_file_actions_adddup2(&res.actions, res.out_pipe[1], 1) != 0)
        {
            return false;
        }
    }
    else if (posix_spawn_file_actions_addopen(&res.actions, 1, "/dev/null", O_WRONLY, 0) != 0)
    {
        return false;
    }
    if (posix_spawn_file_actions_adddup2(&res.actions, res.err_pipe[1], 2) != 0)
    {
        return false;
    }
    // dup2 clears close-on-exec on the copy, so the child keeps the watch pipe's read end (and only that) open across
    // the exec.
    if (res.watch_pipe[0] >= 0 &&
        posix_spawn_file_actions_adddup2(&res.actions, res.watch_pipe[0], posix::kParentWatchFd) != 0)
    {
        return false;
    }
    if (posix_spawnattr_init(&res.attr) != 0)
    {
        return false;
    }
    res.attr_ready = true;
    // Own process group (so a timeout can kill grandchildren too), default signal dispositions and an empty signal
    // mask, so nothing the parent ignores or blocks hides a fault.
    sigset_t all;
    sigset_t none;
    sigfillset(&all);
    sigdelset(&all, SIGKILL);
    sigdelset(&all, SIGSTOP);
    sigemptyset(&none);
    const short extra_flags = posix::PrepareSpawnFlags(&res.actions);
    if (extra_flags < 0)
    {
        return false;
    }
    if (posix_spawnattr_setpgroup(&res.attr, 0) != 0 || posix_spawnattr_setsigdefault(&res.attr, &all) != 0 ||
        posix_spawnattr_setsigmask(&res.attr, &none) != 0 ||
        posix_spawnattr_setflags(&res.attr, static_cast<short>(POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF |
                                                               POSIX_SPAWN_SETSIGMASK | extra_flags)) != 0)
    {
        return false;
    }
    return true;
}

bool IsSanitizerKey(const char* entry)
{
    static const char* const kKeys[] = {"ASAN_OPTIONS", "TSAN_OPTIONS", "UBSAN_OPTIONS", "LSAN_OPTIONS"};
    for (const char* key : kKeys)
    {
        if (KeyMatches(entry, key))
        {
            return true;
        }
    }
    return false;
}

bool BuildEnvironment(EnvironmentBlock& env, const char* name, const char* argument, const ChildOptions& options)
{
    char** const environ_array = posix::ProcessEnvironment();
    usize parent_count = 0;
    for (char** e = environ_array; e != nullptr && *e != nullptr; ++e)
    {
        ++parent_count;
    }
    if (!env.Reserve(parent_count + options.extra_env_count + 8))
    {
        return false;
    }
    const char* const sanitizer_keys[] = {"ASAN_OPTIONS", "TSAN_OPTIONS", "UBSAN_OPTIONS", "LSAN_OPTIONS"};
    for (char** e = environ_array; e != nullptr && *e != nullptr; ++e)
    {
        const char* entry = *e;
        if (KeyMatches(entry, kChildNameVariable) || KeyMatches(entry, kChildArgumentVariable) ||
            KeyMatches(entry, kNoCoreVariable) || KeyMatches(entry, kParentVariable) ||
            KeyMatches(entry, kWatchVariable))
        {
            continue;
        }
        bool replaced = false;
        for (usize i = 0; i < options.extra_env_count; ++i)
        {
            if (KeyOfExtraMatches(entry, options.extra_env[i]))
            {
                replaced = true;
            }
        }
        if (options.sanitizers_pass_faults && IsSanitizerKey(entry))
        {
            replaced = true;  // re-added below, merged with the fault flags
        }
        if (!replaced)
        {
            env.AddBorrowed(*e);
        }
    }
    for (usize i = 0; i < options.extra_env_count; ++i)
    {
        // A sanitizer option the caller passes is merged below instead of added twice (the child would see only the
        // first).
        if (options.sanitizers_pass_faults && IsSanitizerKey(options.extra_env[i]))
        {
            continue;
        }
        if (!env.Add(options.extra_env[i]))
        {
            return false;
        }
    }
    if (!env.Add("ENGINE_DEATH_TEST_CHILD=", name) || !env.Add("ENGINE_DEATH_TEST_ARG=", argument))
    {
        return false;
    }
    char parent[32];
    {
        // The parent's pid as text, so the child can tell whether the parent is still alive (PlatformPrepareChild).
        long pid = static_cast<long>(::getpid());
        usize n = 0;
        char digits[24];
        do
        {
            digits[n++] = static_cast<char>('0' + pid % 10);
            pid /= 10;
        } while (pid > 0);
        for (usize i = 0; i < n; ++i)
        {
            parent[i] = digits[n - 1 - i];
        }
        parent[n] = '\0';
    }
    if (!env.Add("ENGINE_DEATH_TEST_PARENT=", parent))
    {
        return false;
    }
    if (options.disable_core_dumps && !env.Add("ENGINE_DEATH_TEST_NOCORE=1"))
    {
        return false;
    }
    if (posix::ChildNeedsParentWatch() && !env.Add("ENGINE_DEATH_TEST_WATCH=1"))
    {
        return false;
    }
    if (options.sanitizers_pass_faults)
    {
        for (const char* key : sanitizer_keys)
        {
            // The caller's value wins over the parent's own; either way the fault flags are appended after it.
            const char* existing = nullptr;
            for (usize i = 0; i < options.extra_env_count; ++i)
            {
                if (KeyMatches(options.extra_env[i], key))
                {
                    existing = options.extra_env[i] + CStrLength(key) + 1;
                }
            }
            if (existing == nullptr)
            {
                existing = os::GetEnvironment(key);
            }
            const bool is_asan = CStrCompare(key, "ASAN_OPTIONS") == 0;
            const char* flags = is_asan ? kAsanFaultFlags : kOtherFaultFlags;
            const bool has_existing = existing != nullptr && existing[0] != '\0';
            char prefix[64];
            const usize key_length = CStrLength(key);
            MemCopy(prefix, key, key_length);
            prefix[key_length] = '=';
            prefix[key_length + 1] = '\0';
            if (!env.Add(prefix, has_existing ? existing : "", has_existing ? ":" : "", flags))
            {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

namespace detail
{

bool PlatformCanSpawn()
{
    return true;
}

bool PlatformChildRequest(const char** name, const char** argument)
{
    const char* requested = os::GetEnvironment(kChildNameVariable);
    if (requested == nullptr)
    {
        return false;
    }
    const char* arg = os::GetEnvironment(kChildArgumentVariable);
    *name = requested;
    *argument = arg != nullptr ? arg : "";
    return true;
}

void PlatformPrepareChild()
{
    if (os::GetEnvironment(kNoCoreVariable) != nullptr)
    {
        const struct rlimit none = {0, 0};
        ::setrlimit(RLIMIT_CORE, &none);
    }
    // Die with the parent: if the test program is killed (Ctrl-C, a ctest timeout) while a child runs, the child is in
    // its own process group and would otherwise outlive it. The parent might already be gone, so check after arming.
    posix::ArmDieWithParent(os::GetEnvironment(kWatchVariable) != nullptr ? posix::kParentWatchFd : -1);
    const char* parent = os::GetEnvironment(kParentVariable);
    if (parent != nullptr && static_cast<long>(::getppid()) != CStrToLong(parent))
    {
        ::_exit(kUnknownDeathTestExitCode);
    }
}

ChildResult PlatformRunChild(const char* name, const char* argument, const ChildOptions& options)
{
    ChildResult result;
    result.supported = true;

    static const char* const kDefaultArguments[] = {"[.death-child]", "--colour-mode", "none"};
    const char* const* extra_arguments = options.arguments != nullptr ? options.arguments : kDefaultArguments;
    const usize extra_argument_count = options.arguments != nullptr ? options.argument_count : 3;
    Vector<char*> argv;
    if (!argv.TryReserve(extra_argument_count + 2))
    {
        return result;
    }
    char executable_buffer[1024];
    const char* const kExecutable = posix::SelfExecutablePath(executable_buffer, sizeof(executable_buffer));
    if (kExecutable == nullptr)
    {
        return result;
    }
    static char kProgramName[] = "death-test-child";
    argv.Push(kProgramName);
    for (usize i = 0; i < extra_argument_count; ++i)
    {
        argv.Push(const_cast<char*>(extra_arguments[i]));
    }
    argv.Push(nullptr);

    EnvironmentBlock env;
    if (!BuildEnvironment(env, name, argument, options))
    {
        return result;
    }
    char* const* envp = env.Finish();

    SpawnResources res;
    if (!PrepareSpawn(res, options))
    {
        return result;
    }
    pid_t pid = 0;
    if (posix_spawn(&pid, kExecutable, &res.actions, &res.attr, argv.Data(), envp) != 0)
    {
        return result;
    }
    result.started = true;
    // The parent keeps only the read ends; closing the write ends is what lets the pipes report end of file. Of the
    // watch pipe it keeps the write end (the child holds the read end) until `res` goes out of scope, after the child
    // is reaped.
    SpawnResources::CloseFd(res.out_pipe[1]);
    SpawnResources::CloseFd(res.err_pipe[1]);
    SpawnResources::CloseFd(res.watch_pipe[0]);

    int out_fd = options.capture_stdout ? res.out_pipe[0] : -1;
    int err_fd = res.err_pipe[0];
    const u32 timeout_ms = options.timeout_ms != 0 ? options.timeout_ms : kDefaultTimeoutMs;
    // Nanoseconds throughout: a deadline computed from a time truncated to milliseconds would fire up to 1 ms early.
    const u64 deadline = os::MonotonicTimeNanos() + static_cast<u64>(timeout_ms) * 1000000ull;
    int status = 0;
    bool reaped = false;
    bool observed = false;  // true only when waitpid really returned the child's status

    while (!reaped)
    {
        struct pollfd fds[2];
        nfds_t count = 0;
        if (out_fd >= 0)
        {
            fds[count++] = {out_fd, POLLIN, 0};
        }
        if (err_fd >= 0)
        {
            fds[count++] = {err_fd, POLLIN, 0};
        }
        // A grandchild can keep a pipe open after the child is gone, so the child's end is detected by waitpid, not by
        // end of file; the poll slice only bounds how long we sleep between those checks.
        const u64 now = os::MonotonicTimeNanos();
        const u64 remaining_ms = deadline > now ? (deadline - now + 999999ull) / 1000000ull : 0;  // rounded up
        const int slice = static_cast<int>(remaining_ms < kPollSliceMs ? remaining_ms : kPollSliceMs);
        if (count > 0)
        {
            ::poll(fds, count, slice);
        }
        else
        {
            os::SleepNanos(static_cast<u64>(slice) * 1000000ull);
        }
        DrainPipe(out_fd, result.stdout_text, result.stdout_length, result.stdout_truncated);
        DrainPipe(err_fd, result.stderr_text, result.stderr_length, result.stderr_truncated);

        const pid_t waited = ::waitpid(pid, &status, WNOHANG);
        if (waited == pid)
        {
            reaped = true;
            observed = true;
        }
        else if (waited < 0 && os::LastError() != EINTR)
        {
            reaped = true;  // the child is gone and not ours to reap (SIGCHLD ignored, or another thread reaped it):
                            // its fate is unknown
        }
        else if (os::MonotonicTimeNanos() >= deadline)
        {
            result.timed_out = true;
            ::kill(-pid, SIGKILL);
            ::kill(pid, SIGKILL);
            pid_t waited_after_kill = -1;
            do
            {
                waited_after_kill = ::waitpid(pid, &status, 0);
            } while (waited_after_kill < 0 && os::LastError() == EINTR);
            observed = waited_after_kill == pid;
            reaped = true;
            // The child may have finished by itself just before the kill landed: then it did not time out.
            if (observed && !(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL))
            {
                result.timed_out = false;
            }
        }
    }
    // Whatever the child wrote before it died is still in the pipes.
    DrainPipe(out_fd, result.stdout_text, result.stdout_length, result.stdout_truncated);
    DrainPipe(err_fd, result.stderr_text, result.stderr_length, result.stderr_truncated);

    if (!observed)
    {
        return result;  // neither exited nor terminated: the exit status was never seen
    }
    if (WIFEXITED(status))
    {
        result.exited = true;
        result.exit_code = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        result.terminated = true;
        result.native_code = WTERMSIG(status);
        result.reason = ReasonForSignal(WTERMSIG(status));
    }
    return result;
}

}  // namespace detail

void ExitChildProcess(int code)
{
    ::_exit(code);
}

}  // namespace engine_placeholder::debug
