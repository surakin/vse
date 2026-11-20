# Platform: OS Layer

Status: written (Phase 1; Windows from Phase 13; macOS from Phase 21 Day 285).

**Audience:** engine programmers. Read [Standard types](foundation-types.md) and [Memory](foundation-memory.md) first.

The OS layer (`engine/foundation/os/`) is the thin set of operating-system services everything else stands on: writing to the console, the clock, reserving memory,
loading dynamic libraries, reading the environment and the command line, and asking about the user's session. It is deliberately small. Most of what a program
needs from the system has a better home: **files** are in [Files](io.md), **windows and input** are in [Windowing and input](platform-windowing-and-input.md), and
**threads and locks** are in [Synchronization](foundation-sync.md).

## Design: what is portable and what is per-platform

You write against the portable headers (`os/os.h`, `os/console.h`, `os/command_line.h`, `os/session.h`). Behind each there is one implementation per platform in
`os/linux/`, `os/windows/` and `os/macos/`, chosen by the build (Linux and macOS share the POSIX half in `os/posix/`). You never include those directly (a gate fails the build if you do), and you never ask the compiler
"am I on Windows?" yourself: use `#if ENGINE_PLATFORM_WINDOWS` from `build_defs.h`. Text is UTF-8 on every platform; the Windows backend converts to UTF-16 at the
call.

## Console output

Program output (not diagnostics) goes through `os/console.h`: `os::Print(format, ...)` (a `printf`-style format), `os::PrintError`, `os::PrintLine(text)`, `os::Flush()`.
Diagnostics belong in the log ([Logging](foundation-logging-and-assert.md)). You will see `os::Print` in every sample in this guide.

```cpp compile run standalone
#include "os/console.h"

using namespace vse;

int main() {
    os::Print("%s has %d lives\n", "player", 3);
    os::PrintLine("a whole line of text");
    os::PrintError("errors go to standard error\n");
    return 0;
}
```

```output
player has 3 lives
a whole line of text
```

## Time

`os::MonotonicTimeNanos()` returns nanoseconds from a clock that only moves forward and is not affected by the user changing the system time: use it for every
duration (frame time, timeouts). It has no meaning as a date. `os::SleepNanos(n)` sleeps for at least `n` nanoseconds.
`os::WallClockUnixMillis()` is the date: milliseconds since 1970-01-01 UTC. It follows the system clock and can jump, so use it for stamps and calendars, never for durations.

## Random bytes

`os::FillRandomBytes(buffer, count)` fills a buffer from the operating system's secure random source (`getrandom`, `getentropy`, `BCryptGenRandom`). It is for identifiers,
nonces and seeds that must not be guessable; a game's gameplay random numbers want a seeded generator instead.

```cpp compile run standalone
#include "os/console.h"
#include "os/os.h"

using namespace vse;

int main() {
    const u64 start = os::MonotonicTimeNanos();
    os::SleepNanos(2'000'000);                       // 2 ms
    const u64 elapsed = os::MonotonicTimeNanos() - start;
    os::Print("slept at least 2 ms: %s\n", elapsed >= 2'000'000 ? "yes" : "no");
    return 0;
}
```

```output
slept at least 2 ms: yes
```

Sleeping is never early but can be late (by about half a millisecond on Windows), so code that must wait less than that spins instead. A game loop that waits for the next
frame should use the display's own pacing ([Windowing](platform-windowing-and-input.md)), not sleep.

## Memory reservation

`os::ReserveMemory(size)` asks the system for a block of address space and returns its address (or `nullptr`); `os::ReleaseMemory(address, size)` gives it back. Pages
are backed by real memory only when you touch them, so a large reservation is cheap. The arena, stack and pool allocators use it ([Memory](foundation-memory.md)); you rarely call it
yourself.

```cpp compile run standalone
#include "os/console.h"
#include "os/os.h"

using namespace vse;

int main() {
    const usize size = 16 * 1024 * 1024;                  // 16 MiB of address space
    byte* block = static_cast<byte*>(os::ReserveMemory(size));
    if (block == nullptr) {
        os::Print("the system refused\n");
        return 1;
    }
    block[0] = 42;                                         // touching a page makes it real
    block[size - 1] = 7;
    os::Print("first %d, last %d\n", block[0], block[size - 1]);
    os::ReleaseMemory(block, size);
    return 0;
}
```

```output
first 42, last 7
```

## Environment variables

`os::GetEnvironment("NAME")` returns the value or `nullptr`. **Do not use it for configuration.** Anything a user can configure is a *setting*
([Settings](settings.md)), which can come from the environment, a file or the command line; the `config_access_check` gate rejects other uses. It exists for the few
variables that must be read before the settings system can (the window backend, the job thread count, the crash handler). `os::EnvironmentCount()` and
`os::EnvironmentEntry()` walk the whole environment; on Windows names ignore case (`os::EnvironmentNamesIgnoreCase()`).

## Locale

`os::SetLocale(os::LocaleCategory::All or Numeric, name)` (`os/locale.h`) is `setlocale`: it returns the name now in effect, or null when the locale is
not installed, and `nullptr` as the name reads without changing it. The engine never calls it; a host program may, and the locale is process-wide. Code
that stores or parses numbers as text is independent of it ([C strings and formatting](foundation-cstr.md)); tests use it, through
`test::CommaLocale`, to prove that.

## Command line

A windowed program defines `int EngineMain(int argc, char** argv)` and writes `ENGINE_WINDOWED_PROGRAM(EngineMain)` once at global scope instead of `main`
(`platform/entry_point.h`); the macro becomes `main` on Linux and `wWinMain` on Windows (so no console window opens), and the arguments are UTF-8 on all three. A
command-line tool just keeps `main`. Then `os::CommandLine<Allocator>::FromNative(allocator)` parses the arguments: `--name` is a flag, `--name=value` a flag with a value, and
anything else is a positional argument.

```cpp compile run standalone
#include "mem/arena_allocator.h"
#include "os/command_line.h"
#include "os/console.h"

using namespace vse;

int main() {
    // Synthetic arguments, as a program started with: game --fast --level=3 map.dat
    char a0[] = "game";
    char a1[] = "--fast";
    char a2[] = "--level=3";
    char a3[] = "map.dat";
    char* argv[] = {a0, a1, a2, a3};

    ArenaAllocator arena(4096);                            // the strings are copied into the allocator
    os::CommandLine<ArenaAllocator> command_line(arena, 4, argv);

    StringView level;
    os::Print("fast: %s\n", command_line.HasFlag("fast") ? "yes" : "no");
    os::Print("level option: %s\n", command_line.TryGetOption("level", level) ? level.Data() : "none");
    os::Print("positional arguments: %zu, first '%s'\n", command_line.ArgCount(), command_line.Arg(0).Data());
    os::Print("--fast has a value: %s\n", command_line.TryGetOption("fast", level) ? "yes" : "no");
    return 0;
}
```

```output
fast: yes
level option: 3
positional arguments: 1, first 'map.dat'
--fast has a value: no
```

A program that has options of its own declares them, so that `--name value` works too and a typo is an error instead of a silently ignored word: pass a `Span<const os::CommandLineOption>` (`{"name", takes_value}`) as the fourth constructor argument. A flag that takes a value takes the next token (`--root DIR`) or `=value`; an unknown option, a missing value and `--flag=value` for a flag with no value make `Ok()` false and `Error()` say why (the first one found); a bare `--` ends the flags. Tools and demos never index `argv` themselves (`cmdline_access_check`).

```cpp compile run standalone
#include "containers/span.h"
#include "mem/arena_allocator.h"
#include "os/command_line.h"
#include "os/console.h"

using namespace vse;

int main() {
    char a0[] = "tool";
    char a1[] = "--root";
    char a2[] = "some dir";
    char a3[] = "--quiet";
    char a4[] = "input.txt";
    char* argv[] = {a0, a1, a2, a3, a4};

    static const os::CommandLineOption options[] = {{"root", true}, {"quiet", false}};
    ArenaAllocator arena(4096);
    os::CommandLine<ArenaAllocator> command_line(arena, 5, argv, Span<const os::CommandLineOption>(options, 2));

    StringView root;
    command_line.TryGetOption("root", root);
    os::Print("ok: %s, root '%s', quiet %s, positional '%s'\n", command_line.Ok() ? "yes" : "no", root.Data(),
              command_line.HasFlag("quiet") ? "yes" : "no", command_line.Arg(0).Data());
    return 0;
}
```

```output
ok: yes, root 'some dir', quiet yes, positional 'input.txt'
```

In a real program you rarely read flags yourself: declare a setting with a `flag` name and the settings system reads it ([Settings](settings.md)).

## Dynamic libraries and driver probing

`os::LoadLibrary(path)` opens a shared library and returns a handle (`nullptr` on failure), `os::GetSymbol(handle, "name")` finds a function in it, and
`os::UnloadLibrary(handle)` closes it. A relative path with a directory in it is refused on Windows and macOS, and there the current directory is never searched. The engine
uses this to load the GPU driver at run time instead of linking it: `os::VulkanLoaderName()` is `libvulkan.so.1` on Linux and `vulkan-1.dll` on Windows (macOS has no Vulkan: its GPU backend, Metal, is a system framework), and
`os::ProbeVulkanCreateInstance()` loads it and reports (to the log, step by step) whether the machine has a usable Vulkan loader. A machine without one then fails
with a clear message instead of a loader error at start-up.

```cpp
os::DynamicLibraryHandle lib = os::LoadLibrary("libexample.so");
if (lib != nullptr) {
    using AddFn = int (*)(int, int);
    AddFn add = reinterpret_cast<AddFn>(os::GetSymbol(lib, "example_add"));
    if (add != nullptr) { /* call it */ }
    os::UnloadLibrary(lib);
}
```

## Session state

Window tests and the demos need to know whether anyone can see a window. `os::QuerySessionState()` answers `Active`, `Locked`, `Idle`, `Inactive` or `Unknown`
(Linux asks systemd-logind; Windows reads the session and the input desktop; macOS reads the window server's session dictionary). `os::SessionCanShowWindows(state)` is true for `Active` and `Unknown`. Tests that open real
windows skip, with the reason printed, when the session cannot show them; this is why two X11 tests fail while your screen is locked.

## Descriptors and exiting

`os::Write`, `os::Read` and `os::Close` work on file descriptors: they are for the console, pipes and the code that runs inside a crash handler, where the file layer
may not run. For files use `io::File`. `os::Exit(code)` ends the process with an exit code, `os::ExitNow(code)` ends it at once without running exit handlers, and `os::Abort()` ends it as a crash
so the debugger or crash handler sees it.

## Common mistakes

- **Using `getenv`, `printf`, `fopen` or `Sleep`/`usleep` directly.** The gates fail the build; use the wrappers above.
- **Reading configuration from the environment.** Declare a setting.
- **Measuring time with a wall clock.** Use `MonotonicTimeNanos`; the system clock can jump.
- **Assuming sleeps are exact.** They are a minimum.
- **Writing `#ifdef _WIN32` or `__linux__`.** Use `ENGINE_PLATFORM_*` from `build_defs.h`.
- **Including a platform backend header** (`os/windows/...`) from portable code.

## Porting notes (Windows, macOS, mobile, console)

**Windows** (Phase 13; `engine/foundation/os/windows/`). Win32 is the platform contract; the C runtime is used where it is the plain
choice (`std::exit`), never for descriptors.

- **Strings at the boundary.** The engine is UTF-8; Win32 takes UTF-16. `os/windows/utf16.h` converts in both directions into caller
  buffers, counts invalid input (which becomes U+FFFD) instead of hiding it, and allocates nothing. Paths also get `/` turned into `\`, and
  long absolute paths the `\\?\` prefix, so they work whatever `LongPathsEnabled` says.
- **Descriptors** are a table of handles of the engine's own (0-2 the standard handles, 256 more slots claimed without a lock). The C
  runtime's descriptors were rejected because a thread stopped inside `_write` holds a lock that a crash report on another thread would
  wait on forever (Phase 13 Day 147, Spike A). Console output is converted to UTF-16 and written with `WriteConsoleW`; the console's code
  page is left alone, because changing it would outlast the program in the user's shell.
- **`argv` is UTF-8** because every executable carries `cmake/engine.manifest`, which sets the process code page to UTF-8; the C runtime
  builds `argv` with it, and `os::StashNativeCommandLine` keeps it as on Linux. A test starts a child with non-ASCII arguments, quotes and
  backslashes and checks the bytes.
- **Sleeping** uses a high-resolution waitable timer per thread: never early, but on the development PC it wakes in steps of about half a
  millisecond (a 100 µs request took 0.51 ms at best). Code that must wait less spins instead.
- **Libraries** are never searched for in the current directory; a relative path with a directory in it is refused.
- **Session state** comes from `WTSQuerySessionInformation` (inactive) and the input desktop's name (locked); Windows has no idle hint.
- **`include os/windows/win32.h`, never `<windows.h>`**: it removes the macros whose names are engine identifiers (`LoadLibrary`, `near`,
  `far`, ...), so Windows code calls the explicit `...W` functions.

**macOS** (Phase 21; `engine/foundation/os/macos/`, with `os/posix/` shared with Linux). The descriptor, memory, sleep, symbol and
environment functions are the same POSIX calls as on Linux and live once, in `os/posix/os_posix.cpp`; each system's `os.cpp` adds the
rest.

- **The clock** is `CLOCK_UPTIME_RAW` (`clock_gettime_nsec_np`), which, like Linux's `CLOCK_MONOTONIC`, stops while the machine sleeps
  (macOS's own `CLOCK_MONOTONIC` keeps counting). It is in nanoseconds already, so there is no `mach_absolute_time` timebase to
  multiply and nothing to overflow.
- **Pages** are 4 KiB on Intel Macs and 16 KiB on Apple silicon: never assume 4 KiB.
- **Sleeping** is `nanosleep`, resumed after a signal: never early. On the development Mac (Intel, macOS 15) 1 µs took at least 5 µs,
  100 µs at least 107 µs and 1 ms at least 1.06 ms.
- **Libraries** are never searched for in the current directory, which macOS's `dlopen` would do for a bare name: a bare name is looked
  for next to the program, then in `/usr/lib`; a relative path with a directory in it is refused.
- **The environment** is `*_NSGetEnviron()` (a library cannot name `environ` on macOS); `argv` is UTF-8, and a test starts a child with
  non-ASCII arguments, quotes and backslashes and compares the bytes of its arguments and of a log line through a pipe.
- **Process figures** come from `task_info` (resident size) and `proc_pidinfo` (open descriptors).
- **Session state** is the window server's session dictionary (`os/macos/SESSION.md`); a process started over ssh is in no window-server
  session and reports `Unknown`.
- **Objective-C++** (`.mm`, ARC) is used only under `macos/` directories, where an Apple framework has no C interface worth using.

## What the platform layer adds to a crash report

When a window opens, its backend (`wayland`, `x11`, `win32` or `cocoa`) is registered as the `window` line of every crash report. A fault in a window
callback names the thread it ran on, like any other. The crash dump directory comes from `io::CrashDumpDirectory` (on Linux under
`$XDG_STATE_HOME` or `~/.local/state`; on Windows `%LOCALAPPDATA%\<app>\crashes`; on macOS `~/Library/Application Support/<app>/crashes`); `os::CreateFileForWriting` is what the crash report uses to write its file, because it opens it inside the fault
handler where the file layer may not run.

## Descriptors, not files

After Phase 9 the OS layer no longer reads or lists files. `os::Read`, `os::Write`, `os::Close` and `os::WriteAllWithTimeout` are
**descriptor primitives for the console, pipes, sockets and the signal-safe layer**; `os::Open` and `os::CreateFileForWriting` stay only
for two callers that cannot use the file layer: the debugger check that reads `/proc/self/status` (the assert-failure path calls it while
the heap may be inconsistent) and the crash report, which opens its file inside the fault handler. Everything else uses
[`io::File`](io.md): `ReadFileToArena`, `Lseek`, `EnsureDirectory`, `EnsurePrivateDirectory` and `GetCrashDumpDirectory` are gone
(replaced by `io::ReadFile`, `File::Seek`, `io::CreateDirectories`, `io::CreatePrivateDirectory` and `io::CrashDumpDirectory`).
