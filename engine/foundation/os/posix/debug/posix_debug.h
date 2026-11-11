#pragma once

#include <spawn.h>

#include "types.h"

// The hooks the shared POSIX death-test backend (os/posix/debug/death_test_posix.cpp, Phase 26 Day 307) asks of each system:
// os/linux/debug/posix_debug_hooks.cpp and os/macos/debug/posix_debug_hooks.cpp define them. Internal to the debugging backends (module_deps_check:
// os/posix/debug/ is per-platform code owned by os/linux/debug/ and os/macos/debug/).
namespace engine_placeholder::debug::posix {

/// The descriptor number a death-test child finds its parent-watch pipe on (macOS), above the standard three.
constexpr int kParentWatchFd = 3;

/// Makes a pipe whose two ends close on exec (`pipe2(O_CLOEXEC)` on Linux; `pipe` then `FD_CLOEXEC` on macOS, which has no `pipe2`).
/// Returns true, or false with the error in `errno`.
bool MakeCloexecPipe(int fds[2]);

/// The path to start the test program again with: `/proc/self/exe` on Linux (so a binary rebuilt or deleted while running still starts),
/// `_NSGetExecutablePath` on macOS. Null-terminated in `buffer`; null when it does not fit.
const char* SelfExecutablePath(char* buffer, usize capacity);

/// The process environment array (`environ`, `*_NSGetEnviron()`).
char** ProcessEnvironment();

/// Parent side: whether the child needs a watch pipe to notice its parent's death. Linux: false (`PR_SET_PDEATHSIG` does it); macOS: true.
bool ChildNeedsParentWatch();

/// Adds what the system needs to the spawn and returns its extra posix_spawn flags, or -1 on failure. Linux: nothing, 0. macOS:
/// `POSIX_SPAWN_CLOEXEC_DEFAULT` (only the file actions' descriptors survive, closing the window between `pipe` and `fcntl` in MakeCloexecPipe)
/// with standard input inherited explicitly.
short PrepareSpawnFlags(posix_spawn_file_actions_t* actions);

/// Makes the calling thread take a bus error (FaultKind::BusError): Linux touches a page of an empty memfd mapping (SIGBUS); macOS writes into a
/// page mapped with no access, which it delivers as SIGBUS (and has no memfd). Returns only if the fault did not happen.
void TriggerBusError();

/// Child side, from PlatformPrepareChild: arranges for the child to die with its parent. Linux: `prctl(PR_SET_PDEATHSIG, SIGKILL)`. macOS: when
/// `watch_fd` is open, a thread blocks reading it and kills the process at end of file (the parent never writes; its end closes when it exits or
/// is killed, even with SIGKILL).
void ArmDieWithParent(int watch_fd);

} // namespace engine_placeholder::debug::posix
