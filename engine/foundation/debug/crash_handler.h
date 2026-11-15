#pragma once

#include "debug/crash_reason.h"
#include "io/path.h"
#include "types.h"

//! Catching a fatal fault and leaving a minidump of the process behind.
//!
//! Built on sentry-native's Breakpad backend behind this interface (PLAN.md assumption #20); nothing here names the
//! library. The library installs handlers for the fatal signals; when one arrives it writes a minidump (every thread,
//! the registers, the stack memory, the module list) into a database directory on the local disk and the process then
//! dies from the original fault. **Nothing is ever sent anywhere:** the library is built without a network transport
//! and these options have no address to send to.
namespace engine_placeholder::debug
{

/// How the crash handler is set up.
///
/// There is deliberately no field for a server address or an account: the handler only writes files.
///
/// - Thread-safety: thread-compatible. A plain value; the strings are borrowed for the [[debug::InstallCrashHandler]]
/// call.
struct CrashHandlerOptions
{
    /// Directory the dumps go to; null means [[io::CrashDumpDirectory]]. Created if missing. Never the working
    /// directory.
    const char* dump_directory = nullptr;
    /// A file attached to every dump (the crash report text written by the report hook); null for none. The file need
    /// not exist when the handler is installed.
    const char* report_file = nullptr;
    /// The program's version or build id, recorded with every dump; null for none.
    const char* release = nullptr;
    /// Path of a separate handler program for backends that need one; null or empty for the Breakpad backend, which has
    /// none.
    const char* handler_path = nullptr;
    /// True to also allow the platform's own post-mortem artifact (a core file on Linux) and to say in every crash
    /// report where it goes; see [[debug::EnablePostMortemDumps]]. Off by default: a core holds the whole process
    /// memory.
    bool post_mortem_dumps = false;
    /// False to leave the handler off: [[debug::InstallCrashHandler]] then does nothing and returns
    /// [[debug::CrashHandlerResult::Disabled]]. Lets a program keep one call and switch it by configuration.
    bool enabled = true;
    /// How many saved crash dumps (`<dump directory>/saved/*.envelope`) to keep: when the handler is installed, the
    /// oldest beyond this many are deleted. 0 keeps all of them.
    u32 keep_saved_dumps = 20;
};

/// Deletes the oldest `*.envelope` files of a directory so that at most `keep` remain.
///
/// - Param saved: the directory holding the saved dumps; a missing one is not an error.
/// - Param keep: how many of the newest to keep; 0 deletes nothing.
/// - Returns: how many files were deleted.
/// - Fails: returns 0 when the directory cannot be listed; a file that cannot be deleted is skipped.
/// - Thread-safety: thread-compatible. Not signal-safe; called when the handler is installed, not from the crash path.
u32 PruneSavedDumps(const io::Path& saved, u32 keep);

/// What [[debug::InstallCrashHandler]] did.
enum class CrashHandlerResult : u8
{
    Installed,         ///< The handler is installed.
    AlreadyInstalled,  ///< It was installed before; this call changed nothing.
    Disabled,          ///< `options.enabled` is false; nothing was installed.
    DirectoryFailed,   ///< The dump directory could not be found or created; nothing was installed.
    InitFailed,        ///< The library refused to start; nothing was installed.
    Unsupported        ///< This platform has no crash handler.
};

/// Installs the crash handler for the whole process.
///
/// Install it once, early in `main`, after [[debug::InitDebugFacilities]]. A handler a program installs for the same
/// signals **after** this call must call the one it replaced (the library's handler runs only if it is called).
///
/// On Windows, while it is installed, `abort()` neither shows the C runtime's "abort() has been called" dialog (Debug
/// builds) nor calls Windows Error Reporting: the handler writes the report and the dump, and nothing waits for a
/// click.
///
/// - Param options: how to set it up.
/// - Returns: what happened; on anything but `Installed` or `AlreadyInstalled` the program runs on without a handler.
/// - Fails: see [[debug::CrashHandlerResult]]; a failure is logged as a warning, never fatal.
/// - Thread-safety: thread-safe. Not async-signal-safe.
CrashHandlerResult InstallCrashHandler(const CrashHandlerOptions& options);

/// Removes the crash handler and restores the previous signal handling.
///
/// A fault after this call behaves as if the handler had never been installed. Does nothing if none is installed.
///
/// - Thread-safety: thread-safe. Not async-signal-safe.
void UninstallCrashHandler();

/// Gives the calling thread room to handle a fault, so a stack overflow on it still produces a report and a dump.
///
/// A fault handler needs stack to run on, and a thread whose stack is exhausted has none.
/// [[debug::InstallCrashHandler]] does this for the thread that calls it; call this once at the start of every other
/// thread that may run deep (job workers, the I/O thread). The memory is 128 KiB of address space per thread, released
/// when the thread ends.
///
/// - Param name: a short name for the thread, shown in crash reports (`"worker-3"`); copied. Null leaves the name as it
/// is.
/// - Returns: true if the thread can now handle a stack overflow.
/// - Fails: returns false if the crash handler is not installed yet (only the name is set; call this again after
/// installing, or
///   install first) or if the memory or the platform call is refused; the thread still produces reports for other
///   faults.
/// - Thread-safety: thread-safe; it affects only the calling thread. Not async-signal-safe.
/// - Backend: Windows: SetThreadStackGuarantee so a stack overflow can be handled; macOS: the name only (the library
/// takes Mach exceptions, which need no alternate stack).
bool RegisterThreadForCrashHandling(const char* name = nullptr);

/// Gives back what [[debug::RegisterThreadForCrashHandling]] set up for the calling thread (the alternate stack of the
/// signal-based backend). Call it before a thread ends: the stack is not released by a `thread_local` destructor, so
/// that the shared engine library can be unloaded while threads that used it still live. A thread that never registered,
/// or whose registration did nothing, is unaffected.
///
/// - Fails: never.
/// - Thread-safety: thread-safe; it affects only the calling thread. Not async-signal-safe.
void UnregisterThreadForCrashHandling();

/// Reports whether the crash handler is installed.
/// - Returns: true between a successful [[debug::InstallCrashHandler]] and [[debug::UninstallCrashHandler]].
/// - Thread-safety: thread-safe.
bool CrashHandlerInstalled();

}  // namespace engine_placeholder::debug
