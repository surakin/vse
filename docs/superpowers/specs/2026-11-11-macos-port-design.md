# macOS Port + Metal Backend — Design (Phase 26, Day 299)

Confirms or amends the twelve decisions of `plans/phase-26-macos-metal.md` with the experiments run on the Mac on 2026-11-10 and 2026-11-11 (the Day 298
spike and Day 299's three spikes), and records the inventory of Linux-only code, the prerequisites and the side-phase rule. The phase file is the plan;
this file is the evidence. The Windows port's spec (`2026-11-07-windows-port-design.md`) is the model; the RHI concept map is
`2026-11-04-rhi-portability-review.md`, sections 1, 5 and 6.

## 1. Machine and prerequisites

**The Mac:** 2019 MacBook Pro 13" (`MacBookPro15,4`), Intel Core i5-8257U (4 cores, 8 threads, 1.4 GHz), 8 GiB RAM, **Intel Iris Plus Graphics 645,
integrated, the only GPU**, 2560x1600 Retina display (backing scale 2.0); macOS 15.8.1 (24H32); APFS, case-insensitive; 20 GiB free.

**Metal on this GPU** (Day 298): Metal 3 and `MTLGPUFamilyMac2` supported, `Apple7` not; `hasUnifiedMemory` 1, `isLowPower` 1, `isRemovable` 0;
argument-buffer tier 1; read-write texture tier 1; `recommendedMaxWorkingSetSize` 1,536 MiB; `maxBufferLength` 2,048 MiB; dynamic libraries, binary
archives and ray tracing supported; driver compiler `metalfe-32023.622`.

**Toolchain (checked 2026-11-11), all recorded in `BUILD.md`:** Command Line Tools 16.4.0.0.1.1747106510 (no Xcode), Apple clang 17.0.0
(`clang-1700.0.13.5`), macOS SDK 15.5 (older than the running OS), CMake 4.4.4 (MacPorts `cmake-devel`), Ninja 1.13.2 (MacPorts), Python 3.14.10
(MacPorts), lldb 1700.0.9.502, `dsymutil` (CLT), Git 2.39.5 (Apple), MacPorts 2.12.7, `/bin/bash` 3.2.57. **`slangc` v2026.18.2** installed from the
release archive (`slang-2026.18.2-macos-x86_64.tar.gz`, SHA-256 `d09a2993b4ed6d81c0c19ff8bede27944bf73437d982549bd7b2ac5a1a3202b9`) at
`~/tools/slang-2026.18.2`; it mints SPIR-V word-identical to the committed `triangle.vert`, `gbuffer.vert`, `lighting.frag` and `cube.frag`
`*.spv.inc` (151, 735, 1,590 and 105 words). The `slangc` first on `PATH` is `/Applications/kitty.app/Contents/MacOS/slangc`, kitty's own program:
`shaders/CMakeLists.txt:57` (`find_program(SLANGC slangc ...)`) would pick it, so Day 300 adds the macOS install location as a hint and the configure
refuses a `slangc` whose `-v` is not a Slang version.

**Memory:** `~/.claude/projects/-Users-user-src-re/memory` was an empty directory; `scripts/link-claude-memory.sh` (BSD-clean as it is) moved it aside
and linked `claude-memory/` (the empty backup was removed).

## 2. The decisions

1. **Toolchain — confirmed, deployment target amended to macOS 13.3.** Apple clang, CMake and Ninja configure and build the base layer with the existing
   flags and no new warning (Spike A). Apple's libc++ marks floating-point `std::to_chars` available from macOS 13.3, and `cstr/number.h`'s
   `FormatShortest` uses it, so 13.0 would not compile it; Metal 3 needs 13.0 anyway. `CMAKE_OSX_DEPLOYMENT_TARGET=13.3` (the cross-syntax triples,
   `arm64-apple-macos13`, stay: they check syntax only).
2. **The C library and POSIX — confirmed, larger share than expected.** The Linux `os/` and `io/` sources compiled unchanged on macOS except for
   `st_mtim` (Spike A), and 212 of 215 base tests passed with them (the three failures were the spike's own stand-in parser, section 3). The `posix/`
   split (section 5) is therefore mostly a move.
3. **Objective-C++ with ARC — confirmed; the Apple macro wrapper is not needed yet.** All 243 neutral engine headers compile in one Objective-C++
   translation unit after `<AppKit/AppKit.h>`, `<Metal/Metal.h>` and `<QuartzCore/CAMetalLayer.h>`, and before them, with `-fobjc-arc -Wall -Wextra
   -Werror` (Spike B): no macro or type clash. This SDK defines the `AssertMacros` names only with underscores by default (no `check`, `verify`,
   `require`); `nil`, `YES`, `BOOL`, MacTypes `Point`/`Rect`/`Handle` collide with nothing of ours; of the 11,669 macros the three frameworks add, the
   127 our code also spells are the C library's and POSIX's own (`EINTR`, `O_CLOEXEC`, `SIGSEGV`, ...), used on purpose. **Amended:** Day 300 does not
   add `unity/apple_enable.h` / `apple_disable.h` up front; per CLAUDE.md ("Unity builds") a pair is added when a real conflict appears. The
   opaque-pointer pattern (a neutral header with an `Impl*`, the `.mm` holding `id<MTLDevice>` and `id<MTLCommandQueue>` under ARC) ran 20 objects x 50
   GPU submissions with **0 leaks** (`leaks --atExit`) and clean under ASan+UBSan and TSan.
4. **Symbols in dSYMs — not exercised yet** (Day 306).
5. **One Cocoa backend, `NSApplication` without a bundle, our own polled loop — confirmed in part** (Spike C): `NSApplication` with activation policy
   `Regular` and `finishLaunching`, a window whose view backs itself with a `CAMetalLayer` (`makeBackingLayer`), events drained with
   `nextEventMatchingMask:untilDate:distantPast` from our loop, clearing through Metal each frame; Retina scale 2.0; a programmatic resize gave one
   `windowDidResize` and the drawable followed (1800x1064 pixels for a 900x532-point view); `performClose:` (the close button's path) reached
   `windowShouldClose:`, which returned NO so the program decides. **Not confirmed:** activation and key focus from a terminal, and frame pacing, because
   the session was **locked with the display asleep** during the run (`CGSSessionScreenIsLocked` 1, `CGDisplayIsAsleep` 1, idle 1,102 s): the app never
   became active and `nextDrawable` did not pace to the display (3,117 frames in about 3 s, 0.92 ms mean). Rerun with the user present (section 7). **Rerun on Phase 26 Day 311 with the session unlocked: confirmed.** The same program became the active app with
   a key window 104 ms after start, from a terminal, and its frames paced to the display (176 frames in about 3 s, 16.88 ms mean, worst 75 ms
   at the programmatic resize); scale 2.0, the drawable 1800x1064 after the resize, `windowShouldClose:` reached by `performClose:`.
6. **Input — not exercised yet** (Day 312).
7. **Session state — confirmed:** `CGSessionCopyCurrentDictionary` reported the locked screen and the console, `CGDisplayIsAsleep` the sleeping
   display, `CGEventSourceSecondsSinceLastEventType` the idle time (Spike C's diagnosis): `os/macos/SESSION.md` works as written.
8. **No Vulkan on macOS — confirmed, with an amendment for the GPU driver probe.** `os/gpu_driver_probe.h` includes `<vulkan/vulkan_core.h>` in
   `foundation_base`; this Mac has no Vulkan headers (none in `/opt/local`, `/usr/local`, the SDK). The probe's only users are the Vulkan loader and
   `tests/foundation/gpu_driver_loading_test.cpp`. **Amended:** on macOS `os/gpu_driver_probe.cpp` and `vulkan_loader_name.cpp` are not built and the
   test is not configured (the plan said the probe would answer "not available"). `rhi_backends` registers Vulkan unconditionally
   (`engine/rhi/backends/backends.cpp:14`): Day 314 adds an `ENGINE_RHI_HAVE_VULKAN` guard beside `ENGINE_RHI_HAVE_DX12`.
9. **Metal baseline, direct slot binding — confirmed** by the device: argument-buffer tier 1 (Day 298).
10. **Shaders — amended by the user (2026-11-10):** the interim, pending route through the undocumented `-[MTLLibrary serializeToURL:error:]`
    (Day 298's day file; `KNOWN-ISSUES.md`).
11. **Golden images not re-blessed — not exercised yet** (Day 322).
12. **Case-insensitive checkout — confirmed clean:** `git ls-files | sort -f | uniq -di` finds no paths or directories that differ only in case; the
    one test that cares already avoids such names (`tests/io/directory_test.cpp:155`, Phase 13 Day 152); no gate assumes case sensitivity.

## 3. Spike A: the build, failure classes (each becomes a task in the day named)

The Darwin stop removed locally (not committed), a throwaway macOS branch building `engine/foundation` (base and file layers) and `tests/types`, `lang`,
`sync`, `cstr` in plain, asan and tsan, with the **Linux** `os/` and `io/` sources unchanged:

| Class | Example | Day |
|---|---|---|
| Hard stops for Darwin | root `CMakeLists.txt:28`, `engine/foundation/CMakeLists.txt:67`, `engine/platform/CMakeLists.txt:63`; non-Linux stop in `third_party/libbacktrace/CMakeLists.txt:13`, whose message still says "macOS is Phase 9 and Windows Phase 8" (stale numbers: fix with the stop) | 284, 290, 295 |
| Vulkan headers in the base layer | `os/gpu_driver_probe.h:3` | 284 (decision 8) |
| Linux field names | `st_mtim` (macOS: `st_mtimespec`) in `io/linux/file_system.cpp:42` and `directory.cpp:50` | 287 |
| libc++ gaps | `std::from_chars` for `double` is a deleted function in Apple's libc++ (`cstr/number.h:63`, `ParseDoubleExact`); floating `to_chars` needs macOS 13.3 (decision 1) | 285 |
| Crash library not configured | `crash_handler_sentry.cpp:18` (`sentry.h`): expected, the spike skipped sentry | 291 |
| Warnings Apple clang raises that Linux clang does not | **none** in the base and file layers and the four base test suites, unity on and off | - |
| Link flags | none: `-Wl,--build-id` is already Linux-only (`CMakeLists.txt:36`) | - |

**Times on this machine:** `foundation_base` clean unity build 1.3 s (two unity units); every configured target in three flavors (13 test executables)
49 s unity, 67 s non-unity. **Tests:** `ctest -j4`, 212 of 215 passed in 30 s; the three failures were `ParseDoubleExact reads plain decimals only` in
each flavor, caused by the spike's stand-in (`strtod_l` with a C locale reads `0x10` as 16, `from_chars` stops at the `x`): the real implementation
needs the hex guard, and the existing test catches its absence (`tests/cstr/cstr_test.cpp:238`).

**Sanitizers:** ASan and TSan work with Apple clang. **`ASAN_OPTIONS=detect_leaks=1` aborts the process on macOS** ("detect_leaks is not supported on
this platform", exit 134), while `detect_leaks=0` (what the death-test child sets, `os/linux/debug/death_test.cpp:40`) is accepted: any leak option set
for tests must stay Linux-only. `leaks --atExit -- <program>` (`/usr/bin/leaks`) is the macOS leak check and worked in Spike B; whether a ctest uses it
is decided on Day 302.

## 4. Found while measuring (each becomes a task in the day named)

- `shaders/CMakeLists.txt:57` would mint with kitty's `slangc` (section 1) — Day 300.
- `engine/jobs` has a platform file, `jobs/linux/thread_platform.cpp` (CPU list from `sched_getaffinity`, pinning, thread ids, naming), and
  `engine/jobs/CMakeLists.txt:18` picks it for every system that is not Windows — Day 305 (its file said jobs had no platform code: corrected).
- `io/linux/error.cpp:25` relies on `ENOTSUP == EOPNOTSUPP`, true on Linux only (45 and 102 on macOS) — Day 303.
- macOS has no `pipe2`: `os/linux/debug/death_test.cpp:175`, `tests/debug/support/test_support_linux.h` — Day 307.
- `os/linux/os.cpp` reads `/proc/self/statm` (`ResidentMemoryKiB`) and `/proc/self/fd` (`OpenHandleCount`): they compile and fail at run time on macOS
  (`task_info(MACH_TASK_BASIC_INFO)`, `proc_pidinfo(PROC_PIDLISTFDS)`) — Day 301.
- `debug_alloc_tests` uses GNU ld's `-Wl,--wrap=malloc` (`tests/debug/CMakeLists.txt:201`); ld64 has no `--wrap` — Day 306 (interposition with
  `DYLD_INTERPOSE`, a malloc-zone counter, or Linux only, decided there).
- Tests that read `/proc/self/status` as "a file whose size reads 0" (`tests/io/file_test.cpp:208, 264`, `tests/io/async_file_test.cpp:435`) and the
  ELF magic check (`tests/foundation/file_io_test.cpp:26`) — Days 303-304; pinning read back from `/proc/self/task` (`tests/jobs/worker_pool_test.cpp`) —
  Day 305; XDG special folders (`tests/io/file_system_test.cpp:359`) — Day 304.
- The demos' CMake `else()` branches mean "Linux" (`game/CMakeLists.txt:40` XDG profile; the `if(WIN32)` blocks of `rhi_demo`, `renderer_demo`,
  `ui_demo` schedule Wayland and X11 runs) — Day 327.
- `scripts/test-phase.sh:36-38` maps every non-Windows host to `linux`, `:50` uses `nproc`; no bash-4 feature anywhere in `scripts/` — Day 302.
- `tests/phases.txt` refuses a `flavors` line for a phase that has no tests in the map (`tests/phase_labels/check.cmake:110`): the
  `flavors 26 macos: plain,asan,tsan` line is added with the first phase-26 test (Day 300 or 285), not on Day 299.

## 5. The `posix/` split

From the inventory (section 8), the files that move to `posix/` beside `linux/` (whole, or with one or two functions left per platform):
`os/linux/os.cpp` (the descriptor, memory, clock, sleep, library and exit functions; `ResidentMemoryKiB`, `OpenHandleCount` and the environment pointer
stay per platform), `os/linux/debug/death_test.cpp` (with hooks for the pipe, the die-with-parent rule and the program's own path),
`os/linux/debug/fault_injection.cpp` (the bus-error mapping per platform), `io/linux/error.cpp`, `local_file.cpp`, `directory.cpp` and
`file_system.cpp` (executable path, user directories and temp directory per platform). Ten Linux files stay Linux-only with a new `macos/` counterpart
(session, debugger, module list, crash context, crash signal, core dumps, policy layers, jobs' thread platform, and the special-folder and debugger
helpers). Each move edits, in the same commit, `tools/module_deps_check/modules.txt` (`platform-dir` lines), the allow lists that name `linux/` paths
(`file_access_check`, `config_access_check`, `std_usage_check`) and `platform_neutral_check`'s test-name rule (`_posix` beside `_linux`, `_windows`,
`_macos`).

## 6. The side-phase rule (confirmed by the user, 2026-11-11)

Phase 26 runs on the Mac beside the main line, so `CURRENT_PHASE` keeps naming the main line's phase. A `SIDE_PHASES` file lists open side phases (one
number per line); `scripts/test-phase.sh --affected` adds them on a machine that builds their tests, and the summary names them. Their tests carry their
own label (`phase26`); `test_phase_labels` accepts a side phase's label above `CURRENT_PHASE`. The rule is written into CLAUDE.md as proposed on
Day 299; the tooling is Day 302.

## 7. Open after Day 299

- Spike C's activation, key focus and frame pacing, with the session unlocked (the user present): rerun the throwaway program before Day 311 starts.
  **Done on Day 311** (decision 5).
- Day 298's "load elsewhere" task (Apple silicon, another macOS version): waits for the user's new Mac (pending in `KNOWN-ISSUES.md`).

## 8. Inventory of Linux-only code

Surveyed at `59442b97` (2026-11-11). Destinations: **posix/** = moves to `posix/` (built on Linux and macOS), **macos/** = new macOS-only file,
**Linux only** = no macOS file needed; "split" = most of the file moves to `posix/`, one or two functions stay per platform.

### 8.1 `engine/foundation/os/linux/` (Days 301, 306-308, 311)

| file | Linux-specific | macOS counterpart | destination | day |
|---|---|---|---|---|
| `os.cpp` | `/proc/self/statm` (`ResidentMemoryKiB`), `/proc/self/fd` (`OpenHandleCount`), `extern char** environ`; the rest is POSIX (`write/read/open/close`, `poll`, `mmap(MAP_ANONYMOUS)`, `clock_gettime(CLOCK_MONOTONIC)`, `nanosleep`, `dlopen`, `sysconf(_SC_PAGESIZE)`) | `task_info(MACH_TASK_BASIC_INFO)`, `proc_pidinfo(PROC_PIDLISTFDS)`, `_NSGetEnviron()` | split | 285 |
| `session.cpp` | systemd-logind over sd-bus (`dlopen("libsystemd")`) | `CGSessionCopyCurrentDictionary` (`os/macos/SESSION.md`; confirmed, decision 7) | Linux only; `macos/session.mm` | 285 |
| `vulkan_loader_name.cpp` | `libvulkan.so.1` | none (decision 8) | Linux only; not built on macOS | 284 |
| `debug/death_test.cpp` | `pipe2(O_CLOEXEC)`, `prctl(PR_SET_PDEATHSIG)`, re-exec through `/proc/self/exe`; shared `posix_spawn`, `setrlimit`, `waitpid`, `poll` | `pipe` + `FD_CLOEXEC`, a parent-death pipe watched by the child, `_NSGetExecutablePath` | split, 3 hooks | 291 |
| `debug/debugger.cpp`, `debugger_linux.h` | `TracerPid:` in `/proc/self/status` | `sysctl(KERN_PROC_PID)`, `P_TRACED` | Linux only; `macos/debug/debugger.cpp` | 291 |
| `debug/module_list.cpp` | `dl_iterate_phdr`, `<link.h>`, `/proc/self/exe` | `_dyld_*`, `_dyld_register_func_for_add_image` | Linux only; `macos/debug/module_list.cpp` | 290 |
| `debug/crash_context.cpp` | `SYS_gettid`, GNU build-id note, `/proc/cpuinfo`, `/proc/meminfo`; shared `uname`, `sysconf` | `pthread_threadid_np`, `LC_UUID`, `sysctlbyname`, `_NSGetExecutablePath` | Linux only; `macos/debug/crash_context.cpp` | 290 |
| `debug/crash_signal.cpp` | signal recorder ahead of Breakpad, `uc_mcontext.gregs[REG_RSP]`, `sigaltstack` | not used: Breakpad takes Mach exceptions, the report comes from `on_crash` | Linux only; `macos/debug/crash_hook.cpp` | 291 |
| `debug/core_dump.cpp` | `prctl(PR_SET_DUMPABLE)`, `core_pattern`, `limits.conf`; shared `RLIMIT_CORE` | `kern.corefile`/`kern.coredump`, `/cores`, DiagnosticReports; `Limited` | Linux only; `macos/debug/post_mortem.cpp` | 292 |
| `debug/fault_injection.cpp` | `memfd_create` + `mmap` for the bus error; rest portable | an unlinked empty file mapped and touched; `udf` on arm64 | split | 291 |

### 8.2 `engine/foundation/io/linux/` (Days 303-304)

| file | Linux-specific | macOS counterpart | destination | day |
|---|---|---|---|---|
| `error.cpp` | errno table; `ENOTSUP == EOPNOTSUPP` assumed | add `EOPNOTSUPP` (guarded: a duplicate label on Linux), `EAUTH`, `EFTYPE` | posix/ | 287 |
| `local_file.cpp` | none (`open(O_CLOEXEC|O_NOCTTY)`, `pread/pwrite`, `fstat`, `ftruncate`, `fsync`) | `F_FULLFSYNC` in `Sync` (fall back to `fsync`) | posix/ with a per-platform sync hook | 287 |
| `directory.cpp` | `st_mtim` | `st_mtimespec` (a per-platform helper) | posix/ | 288 |
| `file_system.cpp` | `/proc/self/exe`, XDG directories, `st_mtim`, an `O_DIRECTORY|O_NOFOLLOW` errno note to recheck; shared `mkstemp`, `mkdtemp`, `rename`, `unlinkat` walk, `realpath`, `readlink` | `_NSGetExecutablePath` + `realpath`; `~/Library/...` (`io/BACKEND.md`); `$TMPDIR`/`confstr(_CS_DARWIN_USER_TEMP_DIR)`; `F_FULLFSYNC`; `copyfile` | split: `posix/file_system.cpp` + per-platform special directories | 288 |

### 8.3 `engine/foundation/settings/linux/` and `engine/jobs/linux/`

| file | Linux-specific | macOS counterpart | destination | day |
|---|---|---|---|---|
| `settings/linux/policy_layers.cpp` | empty `AddPolicyLayers` | managed preferences; the store is `NSUserDefaults`; `_NSGetEnviron` for the snapshot | Linux only; `macos/user_defaults_store.mm`, `macos/policy_layers.mm` | 293 |
| `jobs/linux/thread_platform.cpp` | `sched_getaffinity`, `cpu_set_t`, `pthread_setaffinity_np`, `SYS_gettid`; empty naming | CPUs `0..hw.logicalcpu-1`; pinning returns false (or `THREAD_AFFINITY_POLICY`, a hint); `pthread_threadid_np`; `pthread_setname_np(name)` (self only) | Linux only; `jobs/macos/thread_platform.cpp` | 289 |

### 8.4 `engine/platform/linux/` (45 files, Linux only; macOS is the Cocoa backend, Days 311-313)

Shared by Wayland and X11: `entry_point_linux.cpp`, `window_host_linux.cpp`, `key_translate.*`, `xkb_keyboard.*`, `keyboard_events.*`, `clipboard_pipe.*`;
`wayland/` (12 files and 15 generated); `x11/` (8 files). The macOS counterparts: `macos/entry_point_macos.mm`, `window_host_macos.mm`, `cocoa_window.mm`,
`cocoa_keys.mm`, `cocoa_clipboard.mm` and the drag-and-drop code.

### 8.5 Platform conditions in shared code

| file:line | does | macOS needs | day |
|---|---|---|---|
| `engine/foundation/io/path.h:27` | Windows or POSIX path style | nothing | - |
| `engine/foundation/debug/crash_event_platform.h:40` | Windows-only crash-hook declarations | a macOS declaration for the `on_crash` context | 291 |
| `engine/foundation/debug/crash_handler_sentry.cpp:142, 244` | Windows writes the report from `on_crash`; elsewhere the signal recorder | the condition becomes Windows or macOS | 291 |
| `engine/platform/entry_point.h:40` | Windows `wWinMain`; else `main` | the `main` form fits; `NSApplication` set up in `RunWindowedProgram` | 295 |
| `engine/rhi/vulkan/vk_loader.h:18`, `vulkan_device_surface.cpp:7, 34` | Wayland/Xlib surfaces when not Windows | not built on macOS | 298 |
| `engine/rhi/backends/backends.cpp:4, 14` | registers Vulkan unconditionally | `ENGINE_RHI_HAVE_VULKAN`, `ENGINE_RHI_HAVE_METAL` | 298, 300 |
| `engine/foundation/os/gpu_driver_probe.h:3` | Vulkan header in the base layer | not built on macOS (decision 8) | 284 |
| `tools/crash_extract/resolve.cpp:99`, `tools/docgen/src/samples.cpp:58, 140, 256, 353` | Windows or POSIX | the POSIX side fits | 286, 292 |

About 70 conditions in `tests/`; the eleven whose non-Windows branch assumes Linux are listed in section 4.

### 8.6 CMake

| file:line | does | macOS needs | day |
|---|---|---|---|
| `CMakeLists.txt:23-32` | target table, Darwin stop | `ENGINE_PLATFORM macos` for x86-64 (arm64 compile-only, Day 327) | 284 |
| `CMakeLists.txt:43-55` | `ENGINE_SPLIT_DEBUG_INFO` with `split-debug.sh` | `-g` and a post-link `dsymutil` | 290, 292 |
| `CMakeLists.txt:101-183` | the Windows block (sentry, Vulkan SDK copy, subdirectories, `return()`) | a macOS list growing one layer per day | 284 |
| `cmake/EngineCompilerOptions.cmake:31-46` | GCC and Clang flags | the same for Apple clang; `OBJCXX` with `-fobjc-arc`; deployment target 13.3 | 284 |
| `engine/foundation/CMakeLists.txt:3, 31, 66-67, 87, 177` | per-platform source lists, Darwin stop, libraries | `posix/` and `macos/` lists; frameworks on the layer that uses them | 285-293 |
| `engine/platform/CMakeLists.txt:62-63` | Darwin stop | `macos/*.mm`, AppKit and QuartzCore, the `Info.plist` section | 295 |
| `engine/rhi/CMakeLists.txt:49, 51, 89, 101` | surfaces, `rhi_dx12`; `rhi_backends` always links `rhi_vulkan` | no `rhi_vulkan` on macOS; `rhi_metal` | 298, 300 |
| `engine/jobs/CMakeLists.txt:16-20, 25` | Windows or else Linux | an explicit macOS branch | 289 |
| `engine/{text,ui,ui_window,ecs,spatial}/CMakeLists.txt` | Windows plain+asan, else three flavors | fits | 294 |
| `third_party/libbacktrace/CMakeLists.txt:12-13` | stop unless Linux (stale message) | `macho.c`, `HAVE_MACH_O_DYLD_H` | 290 |
| `third_party/sentry_native/...` | upstream `APPLE` branches | the macOS private headers (KNOWN-ISSUES) | 291 |
| `shaders/CMakeLists.txt:57, 71, 183` | `slangc` lookup; DXIL on Windows | the install hint and version check; the `.metallib` step | 284, 299 |
| `game/CMakeLists.txt:37-41`, the demos' `if(WIN32)` | XDG profile; Wayland/X11 runs | `HOME`-based profile; Metal and Cocoa runs | 310 |
| `tests/foundation/CMakeLists.txt:44` | window tests on Linux only | `*_macos_test.mm` | 295-297 |
| `tests/rhi:58`, `tests/renderer:37`, `tests/scene:12` | LeakSanitizer options | none on macOS | 298 |
| `tests/debug/CMakeLists.txt:196-256` | `--wrap=malloc`; `strip`; `readelf` no-network check | interposition or Linux only; `strip -S`; `nm -u`/`otool -L` | 290, 292 |

### 8.7 Scripts (`/bin/bash` 3.2, BSD userland; no bash-4 feature anywhere)

| file:line | assumption | macOS fix | day |
|---|---|---|---|
| `test-phase.sh:36-38, 50, 217` | non-Windows means `linux`; `nproc`; default build directory | a `macos` platform, `sysctl -n hw.logicalcpu`, `build-macos`; `SIDE_PHASES` | 286 |
| `crash-report.sh:96` | `readelf -n` build id | `dwarfdump --uuid` | 292 |
| `find-core.sh:35, 42-101` | `core_pattern`, `coredumpctl`, `readelf`, `gdb` | `kern.corefile`, `LC_UUID`, `lldb` | 292 |
| `open-core.sh:24-34` | `gdb -batch` | `lldb -b -c` | 292 |
| `split-debug.sh:7-24` | `objcopy`, debuglink, `readelf` | `dsymutil` + `strip -S` | 292 |
| `store-symbols.sh:18-39` | build-id layout | dSYMs by UUID | 292 |
| `link-claude-memory.sh`, `list-aborts.py`, `std-usage-report.py` | none | fits | - |

### 8.8 Linux-only tests

`tests/debug`: `death_test_linux_test.cpp`, `support/test_support_linux.h` (`pipe2`), `resource_guard_linux.cpp`, `assert_linux_test.cpp`,
`crash_handler_linux_test.cpp`, `crash_report_linux_test.cpp`, `crash_end_to_end_linux_test.cpp`, `core_dump_linux_test.cpp` (with
`debug/core_dump_linux.h`), `crash_report_script_linux_test.cpp`, `program_crash_test_linux_test.cpp`, `job_crash_linux_test.cpp`,
`stack_trace_alloc_linux_test.cpp`: shared as `_posix` where the code is POSIX, `_macos` where it is not (Days 306-308). `tests/foundation`: the
Wayland, X11 and window-layer tests (Days 311-313). `tests/rhi`: the Vulkan tests (not built on macOS; Metal equivalents, Days 316-321). **As configured on Day 314:** the files left out on macOS
are `vulkan_bindings_test.cpp`, `vulkan_buffer_test.cpp`, `vulkan_commands_test.cpp`, `vulkan_compute_test.cpp`, `vulkan_device_test.cpp`, `vulkan_fault_test.cpp`, `vulkan_frame_test.cpp`, `vulkan_instance_test.cpp`, `vulkan_loader_test.cpp`, `vulkan_memory_test.cpp`, `vulkan_pipeline_test.cpp`, `vulkan_strip_test.cpp`, `vulkan_swapchain_test.cpp`, `vulkan_texture_test.cpp`; and the Vulkan cases of the shared files, between `#if ENGINE_RHI_HAVE_VULKAN` (defined by
`rhi_backends` where it links the Vulkan backend): in `adapter_test.cpp` (adapter ids, LUIDs, selection), `device_host_test.cpp` (the leak counts
across a recreation), `shader_test.cpp` (shader modules), `renderer_demo_gpu_test.cpp` (the loss and long-run leak checks),
`swapchain_reconfigure_test.cpp` (the surface-format policy and the presentation cases), `render_graph_gpu_test.cpp` (the synchronization
validation count), and `gpu_fixture.h`/`backend_fixture.h` (the validation counters; without Vulkan the fixture's `Require` skips with "no GPU
backend"). The demos' `*_no_vulkan` ctests are not registered on macOS.

### 8.9 `KNOWN-ISSUES.md` lines naming Phase 26 or macOS

Twenty-one lines, each addressed by a day: the Metal shader items (Day 315), the pending `.metallib` route (Days 298, 315, 328), Apple silicon (Days 327,
311), assumption #11 (Day 328), the AArch64 fault-injection branch (Day 327), Breakpad's macOS headers (Day 307), the cross-syntax coverage gaps (Days
290-291), fullscreen and HDR (Days 311, 321), the macOS file layer (Days 303-304), the input stream, cursor, clipboard and drag and drop (Days 312-313),
and the lldb-dependent debug tests (Day 308).

### 8.10 Wrapper modules and fixed-width types

No platform code in `sync`, `cstr`, `lang` or `os/*.h` beyond the `from_chars` gap (section 3). `types.h` spells `i64` as `std::int64_t`, which is
`long long` on macOS (not `long`): no `%ld` format exists in the tree and `FormatTo` is format-checked, and the spike's build raised no overload
ambiguity. Native `__int128` exists on x86-64 macOS. The `4096` constants are path lengths and capacities; the two page-sized ones
(`fault_injection.cpp:35`, `crash_signal.cpp:110`) are harmless with 4 KiB pages and round up on arm64.
