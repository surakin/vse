# macOS debugging backend (Phase 26)

What Phase 26 implements to bring up the debugging facilities on macOS (Day 306: stack traces, the module list, the crash context; Days 307-308
the rest); Linux is the reference
(`engine/foundation/os/linux/debug/`). Everything else is shared: the facade, the assert family, the report writer, the log ring,
the crash state, the tests. `posix_spawn`, `setrlimit`, signals and `sigaltstack` are the same calls as on Linux, so most of the
Linux files compile with small changes. See `docs/guide/porting-debugging.md` and `tests/debug/BACKEND-CONFORMANCE.md`.

The crash library (Breakpad backend) and libbacktrace list macOS as supported at the pinned versions (audit in the design spec).
**Known limits:** libbacktrace on macOS needs the dSYM next to the binary (or a debug build) for file and line; the library takes
Mach exceptions itself (not signals) on macOS, so the Linux signal recorder is **not** used there; the hardened runtime and
notarization restrict some of what a crash handler may do.

## Functions with a `Backend:` label (a test keeps this list in step with the headers)

### `InitCrashContext`
Build id: the `LC_UUID` load command of the main image (`_dyld_get_image_header(0)`), hex. OS name and version:
`sysctlbyname("kern.osproductversion")`. CPU brand: `machdep.cpu.brand_string` (Apple silicon has it too, `Apple M1` and so on: no fallback
needed, checked on Day 306's review); count: `hw.logicalcpu`; memory: `hw.memsize`. Executable: `_NSGetExecutablePath`. Groups: *crash context*.

As built (Day 306, `os/macos/debug/crash_context_macos.cpp`): as described, nothing allocated (the context may be filled on the assert path); the executable is
`_NSGetExecutablePath`'s path without `realpath`. Compared with `sysctl -n` by `tests/debug/macos/stack_trace_macos_test.cpp`.

### `EnablePostMortemDumps`
`setrlimit(RLIMIT_CORE)` as on Linux; cores are written to `/cores` (`kern.corefile`), which is normally disabled
(`sysctl kern.coredump`), so report `Limited` with that note; the system also writes crash reports to
`~/Library/Logs/DiagnosticReports` (name that in the destination). Groups: *post-mortem*.

### `FaultSupported` and `InjectFault`
As Linux (same signals). Integer divide by zero does not trap on arm64: `FaultSupported(DivideByZero)` is false there.
`IllegalInstruction`: `brk`/`udf` on arm64. Expected reasons as Linux. Groups: *fault injection*.

### `IsDebuggerAttached`
`sysctl` with `CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()` and test `kp_proc.p_flag & P_TRACED`. Groups: *assert*.

**`CrashDumpDirectory`** (moved to the file layer: see `engine/foundation/io/BACKEND.md`).
`~/Library/Application Support/<app>/crashes` (from `NSSearchPathForDirectoriesInDomains` or `$HOME`), created 0700.
Sandboxed apps: the container directory. Groups: *dump directory*.

### `RegisterThreadForCrashHandling`
`sigaltstack` on the thread is still useful for signal-based handlers, but the library uses Mach exception ports, which do
not need it: set the name and return true. Groups: *threads*.

### `InitDebugFacilities` and `ReloadDebugModules`
The module list from `_dyld_image_count`, `_dyld_get_image_name`, `_dyld_get_image_header`, and
`_dyld_register_func_for_add_image` for changes. Groups: *stack trace*.

As built (Day 306, `os/macos/debug/module_list.cpp`; libbacktrace on Mach-O, `third_party/libbacktrace`): each image's range is its `__TEXT` segment
plus the slide, not the span of its segments (in the shared cache those overlap other images: Day 306 saw libobjc claim libsystem_pthread's frames).
The images come from `_dyld_register_func_for_add_image` and `..._remove_image` into a registry under a mutex (walking dyld's list by index races with
`dlclose`: indices shift, and a header can be unmapped mid-walk); a refresh copies the registry, and is still explicit, as on Linux
(`ReloadDebugModules` after `dlopen`). libbacktrace skips `MH_BUNDLE`
images; plug-ins are dylibs. File and line need the dSYM (`engine_dsym`).

### `DemangleSymbol`
`abi::__cxa_demangle`. Groups: *stack trace*.

As built (Day 306): the shared `stack_trace_backtrace.cpp`, unchanged.

### `CurrentThreadId`
`pthread_threadid_np(nullptr, &id)`. Groups: *crash report*.

As built (Day 306): the low 31 bits, never 0, so the crash report and the job system's logs (`jobs/macos/thread_platform.cpp`) print one number.

### `DeathTestsSupported` and `RunInChild`
`posix_spawn` shared with Linux; `PR_SET_PDEATHSIG` does not exist on macOS: the child watches a pipe from the parent and exits when it closes (a pipe
reaches EOF even if the parent is killed; an `atexit` handler would not run then). `RLIMIT_CORE` and signal decoding as Linux. Groups: *death tests*.

## Pieces without a neutral label

- **Crash handler build:** `SENTRY_BACKEND=breakpad`, `SENTRY_TRANSPORT=none`. The library's `on_crash` hook receives a
  `ucontext` on macOS (unlike Linux Breakpad, where it is null); write the report from the hook and do not install the Linux
  signal recorder. Stack overflow: Mach exceptions are delivered on a separate port thread, so no alternate stack is needed.
- **`detail::InstallCrashEventHook`** has no macOS implementation; `OnCrashEvent` is the `on_crash` hook.
- **Scripts:** the Linux shell scripts mostly work (`lldb` is the system debugger, `dsymutil`/`strip` replace `objcopy`; the
  symbol store holds dSYM bundles named by UUID). `find-core.sh` and `open-core.sh` need `/cores` rules instead of
  `coredumpctl`.
- **CMake:** replace the stop in `engine/foundation/CMakeLists.txt`; no `--build-id` (the linker writes `LC_UUID`); build with
  `-g` and `dsymutil` for readable traces. libbacktrace uses `macho.c`.
