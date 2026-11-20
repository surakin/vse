#pragma once

#include "diagnostics.h"

// The child processes `docgen samples` starts: the compiler, and each `run` sample's program (Phase 13 Day 161). One
// interface, two implementations: child_process_posix.cpp (posix_spawn, a process group per child) and
// windows/child_process_windows.cpp (CreateProcessW, a job object per child). Output goes to files, never to pipes, so
// a child that writes a lot never blocks on a full pipe the runner is not reading. Every argument view must be followed
// by a NUL in memory (the arena's strings are).
//
// Thread-safety: not thread-safe; the runner drives its children from one thread.
namespace docgen
{

/// A started child and what holds everything it starts in turn.
struct ChildProcess
{
    /// POSIX: the process id, which is also the id of its process group. Windows: the process id. -1 when nothing runs.
    vse::i64 id = -1;
    /// Windows: the process handle. Unused on POSIX.
    void* process = nullptr;
    /// Windows: the job object holding the process and its descendants (closing it kills them). Unused on POSIX.
    void* job = nullptr;
};

/// How a child ended.
struct ChildEnd
{
    /// It returned an exit code.
    bool exited = false;
    /// The exit code, when `exited`.
    vse::i32 exit_code = 0;
    /// It was ended by a signal (POSIX) or by an exception (Windows: an exit code of 0xC0000000 or more, an NTSTATUS
    /// such as an access violation).
    bool killed = false;
    /// The signal number or the exception code, when `killed`.
    u32 cause = 0;
};

/// Starts `args[0]` with the arguments `args` (the first is the program).
///
/// A program named without a directory is searched for as the platform does: `PATH` on POSIX; on Windows
/// CreateProcessW's order, which looks in the program's own and the current directory before `PATH`, so pass an
/// absolute path there (the build always does). Standard input is the null device; standard output goes to `out_path`,
/// created or truncated; standard error goes to `err_path`, or to the same file when `err_path` is empty.
/// - Param working_dir: the child's working directory, or empty for the caller's.
/// - Param arena: scratch for the conversion of the arguments.
/// - Returns: true and fills `out` if the child started.
/// - Fails: returns false if a file cannot be opened or the program cannot be started.
bool StartChild(const Vector<StringView, ArenaAllocator>& args, StringView out_path, StringView err_path,
                StringView working_dir, ArenaAllocator& arena, ChildProcess* out);

/// What [[docgen::PollChild]] found.
enum class ChildPoll : vse::u8
{
    Running,  ///< The child has not ended yet.
    Ended,    ///< It ended; `end` says how and the child is released.
    Lost,     ///< It cannot be waited for (reaped elsewhere); it was killed and released, and how it ended is unknown.
};

/// Returns at once whether the child has ended; when it has, fills `end` and releases the child.
ChildPoll PollChild(ChildProcess& child, ChildEnd* end);

/// Kills the child and everything it started, waits for it to end and releases it.
void KillChild(ChildProcess& child);

}  // namespace docgen
