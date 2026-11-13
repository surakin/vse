# macOS

Status: written (Phase 26, Day 328).

**Audience:** anyone who builds, runs or debugs the engine on a Mac. Read [Getting started](getting-started.md) and
[Building and testing](building-and-testing.md) first: everything in the other guides applies, and this page is only what is different on macOS.

macOS runs the same code as Linux with a Cocoa window and a Metal graphics backend; there is no Vulkan on macOS (Phase 26 decision 8: MoltenVK is
a third-party layer). It is built and tested on an Intel Mac (a 2019 MacBook Pro with Intel Iris Plus Graphics 645); Apple silicon is compiled, not
run (below).

## At a glance

| I want to | Do this |
|---|---|
| build | `cmake --preset macos-clang` then `cmake --build --preset macos-clang` (Apple clang from the Command Line Tools; no Xcode) |
| run tests | `scripts/test-phase.sh --affected` (plain, asan and tsan; no LeakSanitizer) |
| check Apple silicon | `cmake --preset macos-clang-arm64` and build it: compile-only, nothing runs |
| pick the graphics API | `auto` is Metal; `--rhi metal\|null` at start (not changeable while running) |
| turn on GPU validation | `MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=assert MTL_SHADER_VALIDATION=1` in the environment (`--validation` cannot turn it on) |
| mint shaders | `cmake --build build-macos --target shaders` with `slangc` v2026.18.2 in `~/tools` |
| read a crash | `scripts/crash-report.sh <envelope> <program> <symbol store>` (`lldb`, `crash_extract`) |
| find user files | `~/Library/Application Support/<app>` (settings, crashes), `~/Library/Caches/engine` (pipeline cache) |

## Source of truth

- `BUILD.md` ("macOS"): the prerequisites, each with the version in use and how to install it.
- `CMakePresets.json` (`macos-clang`, `macos-clang-arm64`), the macOS branch of the root `CMakeLists.txt`, `cmake/EngineCompilerOptions.cmake`.
- `docs/superpowers/specs/2026-11-11-macos-port-design.md`: the decisions of the port and the measurements behind them.
- `plans/phase-26-macos-metal.md` and its day files (the Metal backend's design and every measurement: Days 315-325).

## Prerequisites

The Command Line Tools for Xcode (`xcode-select --install`: Apple clang, `lldb`, `dsymutil`, the macOS SDK), CMake and Ninja; `clang-format` 23.x
to commit (MacPorts `clang-23`, linked as `clang-format`: the Command Line Tools' Apple `clang-format` formats differently); `slangc` v2026.18.2 only to
mint shaders. Versions and install lines are in `BUILD.md`. Xcode itself is optional (Instruments, the Metal debugger).

## Building

- **The preset** `macos-clang` configures `build-macos` (Ninja, Debug, x86-64, deployment target macOS 13.3). The configure takes Apple clang
  through `xcrun`, so a MacPorts or Homebrew clang first on `PATH` is not picked up. A Release tree is configured the usual way
  (`-DCMAKE_BUILD_TYPE=Release`); measure only there.
- **Flavors:** plain, asan (ASan with UBSan) and tsan, as on Linux. Apple clang has no LeakSanitizer (ASan aborts when asked for `detect_leaks=1`);
  `leaks --atExit -- <program>` is the macOS tool.
- **Both unity modes** build clean, as everywhere (`-DENGINE_UNITY_BUILD=OFF` in a second tree).
- **Apple silicon, compile-only.** `macos-clang-arm64` (`build-macos-arm64`, `ENGINE_COMPILE_ONLY`) builds every target for arm64 on the Intel Mac:
  no test discovery, no shader minting (the committed outputs), testing not enabled. Run it in both unity modes on a day that changes macOS
  code. What it does not cover is in `KNOWN-ISSUES.md`.

## Where macOS code goes

- **Directories:** Apple's headers (the frameworks, `<mach/...>`, `<dispatch/...>`, `<objc/...>`, `<sys/sysctl.h>`, `<copyfile.h>`, `<libproc.h>`)
  and `.mm` files live only in a `macos/` directory or in `engine/rhi/metal/`; rule 7 of `platform_neutral_check` enforces it. `net/macos/` is the
  only place with macOS socket code (`net_access_check`), and only `engine/rhi/metal/` and `tools/metal_mint/` may use Metal (`rhi_boundary_check`).
- **Neutral code asks `build_defs.h`** (`ENGINE_PLATFORM_MACOS`, `ENGINE_ARCH_ARM64`), never `__APPLE__`.
- **Objective-C++ with ARC.** `.mm` files compile with `-fobjc-arc` and every warning of the build. Keep Objective-C objects out of neutral headers:
  a backend header that holds `id<MTL...>` members is included only by Objective-C++ files. Out-pointers to objects are `__autoreleasing` by default;
  write `__strong*` when the callee only stores into it.
- **Autorelease pools.** A caller of the engine may have no pool (a job thread, a C++ test). Every backend entry point that calls into AppKit or
  Metal wraps its body in `@autoreleasepool`, or what Metal autoreleases is never released (it held devices alive: about 37 MiB over 100 resets,
  Day 325). The ctest `metal_autorelease_pools` checks it.
- **Apple's macros.** `AssertMacros.h` defines short names (`check`, `verify`, `require`) unless `__ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES`
  is 0, which the build defines (`ENGINE_MACOS_DEFINITIONS`). AppKit's QuickDraw headers declare global `Handle` and `Pattern`: import AppKit before
  any engine name is in scope in a unity-merged file (`tests/rhi/macos/metal_test_support.h`).

## Running tests

`scripts/test-phase.sh` runs under the system's `/bin/bash` 3.2 and defaults to `build-macos`; `--flavor all` is plain, asan and tsan, and
`--affected` adds phase 26 while it is a side phase. **Window tests need the console unlocked**: a locked Mac (or a VNC session that ended, which
relocks it) gives no window activation and no vsync, so check the summary for skipped `display` tests. The Metal tests run with Metal's
validation in assert mode (`MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=assert MTL_SHADER_VALIDATION=1`, set by the ctests), so a validation error
stops the test. The pipeline archive is off while shader validation is on (Metal stops inside it), so the archive tests (tag `[archive]`) run in a
ctest of their own with API validation alone. Set `ENGINE_RHI_REQUIRE_GPU=1` to make a GPU test that cannot get a device fail instead of skip.

## Running the demos

`rhi_demo`, `renderer_demo` and `ui_demo` open a Cocoa window, or render offscreen with `--screenshot`; `auto` is Metal. Their READMEs have the
macOS lines. The command shortcuts are Command on macOS (`os::kModShortcut`: Cmd+C, Cmd+V, Cmd+Q); F11 is taken by macOS (Show Desktop) before a
program sees it, and the F-keys may need **fn** unless "Use F1, F2, etc. keys as standard function keys" is on.

## Metal

- **Shaders** are minted to `.metallib` (AIR) through the driver's compiler by `tools/metal_mint`, with Slang's Metal output; an interim route (the
  undocumented `serializeToURL:`), decided again on a new Mac (`KNOWN-ISSUES.md`). A Mac without `slangc` uses the committed outputs.
- **Slots:** Metal has argument tables, not descriptor sets; each minted library carries its slot table, and the backend binds directly (no
  argument buffers). A `float3` in a CPU-written block takes 16 bytes in Metal: put no scalar right after one (`shaders/README.md`;
  `shader_mint` refuses a layout that differs from the SPIR-V build).
- **Validation is the process's:** Metal reads `MTL_DEBUG_LAYER` when it starts, so `renderer.validation` changes are logged as taking effect at the
  next start. `sync_validation` has no Metal counterpart.
- **Presentation** is the window's `CAMetalLayer`; `Mailbox` falls back to `Fifo`; EDR HDR is granted only on a screen that has headroom.
- **Device loss** comes from command-buffer errors and the device observer (an external GPU removed); a GPU whose removal was requested is left out
  of the next pick.

## Crash dumps and symbols

Breakpad writes minidumps through sentry-native, as on Linux. **dSYMs:** the linker leaves the DWARF in the object files, so `engine_dsym` runs
`dsymutil` after linking (`ENGINE_DSYM`, on by default) and puts `<binary>.dSYM` next to the binary, where libbacktrace looks; without it a frame has
its function but no file or line. Keep the dSYM with the binary, and file it in the symbol store (`scripts/store-symbols.sh`, bundles named by
UUID). A crashed process exits with Breakpad's exception type (a segfault 1, an abort 5), not by the signal. Details:
[Porting the debugging facilities](porting-debugging.md) ("macOS").

## Input, the cursor, the clipboard and drag and drop

The Cocoa window gives the same event stream as the other backends (`engine/platform/BACKEND.md`): Option is Alt and Command is Super; press-and-hold
is turned off for the program so a held key repeats; text arrives through the input method's insertion (no composition shown); a trackpad's scroll
is 10 points to a notch; relative motion is accelerated (AppKit gives no raw deltas). The clipboard is the general pasteboard (text, files and custom
types), drag and drop is AppKit's dragging session, and the pointer lock disassociates the cursor from the mouse.

## Where things live

| What | Where |
|---|---|
| The user's settings | `~/Library/Application Support/<app>/settings.cfg` (the same file and format as on Linux); a program may use user defaults instead (`settings::UserDefaultsConfigStore`) |
| Crash dumps | `~/Library/Application Support/<app>/crashes` |
| The pipeline cache | `~/Library/Caches/engine/metal-<adapter id>.bin` (a binary archive behind a header that names the adapter and the macOS build) |
| The socket backend | `engine/net/macos/` (the POSIX backend's hooks and a kqueue poller, Phase 26 Day 326; `docs/guide/net.md`, "On macOS") |

## Common mistakes

- **Formatting with the Command Line Tools' `clang-format`.** It formats some constructs differently and ignores `.clang-format-ignore`; link
  MacPorts' 23.x as `clang-format`.
- **Expecting `--validation` to turn Metal's validation on.** Set the environment variables before the program starts.
- **Running window tests over VNC and disconnecting.** macOS locks the console, and the window tests fail or skip.
- **A `slangc` that is not Slang.** kitty ships an unrelated `slangc`; the configure refuses a program that does not answer `-v` with a Slang version.
- **Calling Metal from a new entry point without `@autoreleasepool`.** It leaks whatever Metal autoreleases.
- **Expecting an `accept` out of descriptors to keep the connection.** macOS drops it (Linux keeps it queued).

## See also

[Building and testing](building-and-testing.md), [RHI](rhi.md), [RHI shaders](rhi-shaders.md), [Porting the debugging facilities](porting-debugging.md),
[Platform: windowing and input](platform-windowing-and-input.md), [Networking](net.md), [Windows](windows.md).

## Known gaps

`KNOWN-ISSUES.md` has the current list. The large ones: Apple silicon is compiled, not run; the `.metallib` minting route is interim; no argument
buffers; HDR on a real EDR display, a move between screens of different scale and an external GPU's removal are not seen; no IME composition; no
LeakSanitizer.
