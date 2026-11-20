# Phase 13 / Day 148 — CMake and the Compiler on Windows

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 147 (Spike C's failure list, the toolchain decision).

Make the tree configure and compile with MSVC (and `clang-cl`) up to the platform code, which arrives tomorrow. Shared code changes today, so the Linux
machine must stay green.

## Tasks

- [x] **Root `CMakeLists.txt`:** replace the "Only x86-64 Linux" stop with a supported-platform check (Linux x86-64, Windows x86-64; anything else stops
      with a message naming the phase that ports it); `-Wl,--build-id` stays Linux; Windows links with `/DEBUG:FULL` and compiles with `/Zi` (or `/Z7`
      under sccache, recorded) so every program has a PDB and a CodeView record; `ENGINE_SPLIT_DEBUG_INFO` documents that Windows always has separate PDBs.
- [x] **Per-compiler options:** `cmake/EngineCompilerOptions.cmake` defines `ENGINE_PRODUCTION_COMPILE_OPTIONS` and the warning set per compiler (GCC/Clang:
      today's flags; MSVC and clang-cl: `/permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHs-c- /GR- /W4` plus `_HAS_EXCEPTIONS=0`, and the
      stack-protector equivalent `/GS-` only where Linux uses `-fno-stack-protector`), the sanitizer options (`foundation_asan`: `/fsanitize=address` on
      MSVC; `foundation_tsan` and the tsan canary are not defined on Windows, and the targets that link them are skipped with a configure message), and
      `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `UNICODE`, `_UNICODE` for Windows targets. Catch2's targets keep exceptions on.
- [x] **The build finds its compilers by itself** (developer requirement, 2026-11-07): configuring from a plain PowerShell or Git Bash, with no Developer
      Command Prompt, no `vcvars*.bat` and no `INCLUDE`/`LIB`/`PATH` set by hand, must work. Toolchain files `cmake/toolchains/windows-msvc.cmake` and
      `windows-clang-cl.cmake` locate everything themselves:
      - Visual Studio with `vswhere.exe` (at its fixed path `%ProgramFiles(x86)%/Microsoft Visual Studio/Installer/`) and
        `-latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`, so Build Tools, Community, Professional
        and Enterprise all qualify; the MSVC version from `VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt`, `cl.exe` and `link.exe` from
        `VC/Tools/MSVC/<version>/bin/Hostx64/x64/`.
      - The Windows SDK from the registry (`HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots`, `KitsRoot10`) with the newest version directory under
        `Include/` that has `um/windows.h`; `rc.exe` and `mt.exe` from its `bin/<version>/x64/`.
      - `clang-cl.exe` and `lld-link.exe` from Visual Studio's `VC/Tools/Llvm/x64/bin` first, then an LLVM install (`HKLM\SOFTWARE\LLVM\LLVM`, then
        `%ProgramFiles%/LLVM/bin`). The PC's LLVM 23.1.5 (winget `LLVM.LLVM`) writes no registry key and is not on `PATH`, so the last fallback must work and
        is the case the stripped-environment test exercises; the found `clang-cl` uses the MSVC headers and libraries found above (`/imsvc`, `/libpath`), and
        its version is printed at configure time. `int128.h`: `clang-cl` takes the native `__int128` path, MSVC the portable one; both are built and tested.
      - The include and library directories (MSVC `include`/`lib/x64`, SDK `ucrt`, `um`, `shared`, `winrt` and their `Lib/<version>/{ucrt,um}/x64`) set through
        `CMAKE_<LANG>_STANDARD_INCLUDE_DIRECTORIES` and the link directories, and the tool directories added to the build's environment where the tools need
        them (ASan's runtime DLL beside the test executables, so tests run without a developer `PATH`).
      - Overrides for unusual machines: `ENGINE_VS_INSTALL_DIR`, `ENGINE_WINDOWS_SDK_DIR`/`ENGINE_WINDOWS_SDK_VERSION`, `ENGINE_LLVM_DIR` cache variables;
        a clear configure error that names what was searched when something is missing.
      - If the environment already comes from a Developer Command Prompt, the found compiler must be the same one (or the override wins, with a message), never a
        silent mix of two toolsets.
      - A ctest (`windows_toolchain_discovery`, phase 13) configures a scratch project with the environment stripped of `INCLUDE`, `LIB`, `LIBPATH`,
        `VCINSTALLDIR`, `WindowsSdkDir` and the VS directories on `PATH`, for both toolchain files, and builds and runs a hello-world that includes `windows.h`.
- [x] **CMake presets** (`CMakePresets.json`): `linux` (today's defaults), `windows-msvc` (Ninja, the MSVC toolchain file), `windows-clang-cl` (Ninja, the
      clang-cl toolchain file), each with a build directory of its own; Ninja itself is found on `PATH` or through `ENGINE_NINJA` (the one tool the developer
      installs on `PATH`); `STATUS.md` build commands updated for both platforms (no developer prompt in them).
- [x] **The application manifest:** `cmake/engine.manifest` (UTF-8 active code page, `longPathAware`, per-monitor-v2 DPI, Windows 10/11 `supportedOS`) and a
      function `engine_add_manifest(<target>)` called from every `add_executable` helper, so tests, tools and demos all carry it.
- [x] **`windows.h` wrapper:** `engine/foundation/unity/windows_enable.h` / `windows_disable.h` (push and pop the macros `windows.h` defines that collide with
      engine names: at least `LoadLibrary`, `CreateWindow`, `GetObject`, `CreateFile`, `CopyFile`, `MoveFile`, `DeleteFile`, `CreateDirectory`, `RemoveDirectory`,
      `GetCurrentDirectory`, `SetCurrentDirectory`, `GetEnvironmentVariable`, `near`, `far`, `min`, `max`, `ERROR`, `DELETE`, `IN`, `OUT`, `OPTIONAL`, `interface`;
      the exact list is Spike C's) and `engine/foundation/os/windows/win32.h`, the one header Windows code includes. A test (`windows_header_test.cpp`,
      phase 13) includes `win32.h` and then every foundation header in one unity batch and checks it compiles; a fixture with the wrapper removed fails to
      compile (a `WILL_FAIL` build test, as Phase 10 does for the type checks).
- [x] **Per-platform flavor lines (moved here from Day 150, found on Day 147):** this day's tests are the first of phase 13 and exist only on Windows, so a
      plain `flavors 13 plain,asan` line would make `test_phase_labels` fail on Linux (phase 13 has no tests there). `tests/phases.txt` accepts a platform
      qualifier on a `flavors` line (`flavors 13 windows: plain,asan`), `test_phase_labels` checks it only on that platform, and its self-tests cover both
      platforms; the rest of the platform rule (platform-only test files) stays on Day 150.
- [x] **The two spellings Spike C found behind 38 of the 43 failures:** an `ENGINE_PRINTF_FORMAT(fmt_index, args_index)` macro in `debug/compiler.h` (the GCC
      attribute on GCC/Clang, empty on MSVC, `_Printf_format_string_` on the format parameter where it helps) replaces `__attribute__((format(...)))` in the ten
      files the spec lists, and `jobs/job.h` uses `compiler.h`'s trap macro instead of `__builtin_trap`. `_CRT_SECURE_NO_WARNINGS` for every Windows target
      (C4996 on `strncpy`/`getenv`: the C library is used on purpose); C4324 off for `jobs/deque.h`'s aligned struct; fix the C4649 and C4189 sites. Decide
      whether `/WX` is on, matching Linux's rule.
- [ ] **`clang-cl` and `__int128`:** division and remainder of the native type call compiler-rt (`__udivti3`, `__umodti3`), which `clang-cl` does not link
      against the MSVC runtime; the `clang-cl` configuration links `clang_rt.builtins-x86_64.lib` from the LLVM install the toolchain file found
      (`lib/clang/<major>/lib/windows/`). Test: `tests/types` in the `clang-cl` build (native path) and the MSVC build (portable path).
- [x] **Compiler portability in shared headers:** `debug/compiler.h` gains MSVC spellings (`__forceinline`, `__declspec(noinline)`, `__assume`,
      `_ReturnAddress`, `__debugbreak`, `_BitScanForward64`/`__lzcnt64`/`__popcnt64` behind the existing macro names, `__restrict`), the printf-format
      attribute becomes a macro that is empty on MSVC (with `_Printf_format_string_` where it helps), `int128.h` takes the portable path on MSVC
      (`_umul128`/`_addcarry_u64` intrinsics where the portable code allows, measured against the plain portable code), `types.h` checks its static asserts
      under MSVC (`long` is 32 bits on Windows: audit every `long` in shared headers; `os::Write`/`Read` return `long` today — keep or change to `isize`,
      decided here and recorded).
- [x] **Every other failure class from Spike C** fixed at its cause in shared code (no `#ifdef _MSC_VER` scattered through engine code: a portability
      helper in `compiler.h` or a per-platform source file instead); POSIX-only sources are excluded from the Windows source lists, not stubbed.
- [ ] **CMake minimum 3.21:** presets that name a `toolchainFile` need preset schema 3, which needs CMake 3.21; raise `cmake_minimum_required` and the
      version in `BUILD.md` (check the Linux machine's CMake first).
- [x] **Third-party code on Windows:** libbacktrace is not configured on Windows (Linux-only, decision 5); sentry-native and `foundation_debug` (and every
      target that links it) are not configured on Windows until Day 154 vendors Breakpad's Windows files, and configure says so in one message. The
      libraries below the debugging layer (Phase 12's `foundation_base`, `foundation_io`) do not need either.
- [x] **Build check:** `cmake --preset windows-msvc && cmake --build --preset windows-msvc --target foundation_base` compiles the base layer's platform-free
      sources (everything but its `os/` backend, which is Day 149), unity on and off.
- [x] The `clang-cl` preset compiles the same set; record the warnings it adds that MSVC did not.
- [ ] **Linux:** both unity modes build; `scripts/test-phase.sh --affected` (a `CMakeLists.txt` and foundation headers changed, so everything runs) and the
      asan and tsan flavors; quote the lines.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: macros that silently change meaning between compilers (`ENGINE_ASSUME`, the bit-scan
      helpers' behavior at zero, `long` widths), and the manifest being on every executable.
- [x] Update `BUILD.md` (the Windows status line: the tree configures; the override variables as implemented), `STATUS.md` (build commands for both platforms) and `KNOWN-ISSUES.md` (the remaining Windows link failures until Day 149); commit and push.

**Done:** the tree configures with MSVC and `clang-cl` from a plain shell (the toolchain files find Visual Studio, the Windows SDK and clang-cl themselves,
proven by the stripped-environment test), the platform-free foundation compiles on Windows in both unity modes, the `windows.h` wrapper and its
test exist, the portable `u128` path compiles under MSVC, and the Linux suite still passes in every flavor.

## Result (2026-11-07, on the Windows PC)

- **Configures and builds on Windows from a plain shell.** `cmake --preset windows-msvc` (MSVC 19.51) and `windows-clang-cl` (Clang 23.1.5) configure with no
  Visual Studio variable in the environment; the toolchain files (`cmake/toolchains/windows-common.cmake`, `windows-msvc.cmake`, `windows-clang-cl.cmake`) find
  Visual Studio with `vswhere`, the SDK from the registry, `clang-cl` in `C:\Program Files\LLVM`, and also the Vulkan SDK (needed: `os/gpu_driver_probe.h`
  includes `vulkan_core.h`; only its `vulkan/*.h` and `vk_video/` headers are copied into the build tree, so the SDK's slang, shaderc and dxc headers are not
  on any include path). A Developer Command Prompt for another MSVC or Windows SDK stops the configure (an explicit override wins with a warning); checked
  inside `vcvars64` with the same toolset (passes), a faked `WindowsSDKVersion` (stops) and the faked one plus `ENGINE_WINDOWS_SDK_VERSION` (passes).
- **Windows builds one layer per day:** the root `CMakeLists.txt` adds only `engine/foundation` (just `foundation_base` and `foundation_base_asan`, with no
  `os/windows/` backend yet), `tests/windows` and Catch2, and says in one configure message what is not yet ported and on which day. libbacktrace is not
  configured on Windows; sentry-native and the layers above the base wait for their days.
- **Compiler options per compiler** (`cmake/EngineCompilerOptions.cmake`): GCC/Clang unchanged; MSVC and clang-cl: `/utf-8 /permissive- /Zc:__cplusplus
  /Zc:preprocessor`, `NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS` for the engine's targets only (applied after `third_party/`), production
  `/EHs-c- /GR- /GS- /D_HAS_EXCEPTIONS=0 /W4 /wd4324`, `ENGINE_EXCEPTIONS_COMPILE_OPTIONS` (`/EHsc`) for Catch2 and tools that need it, `/Zi /FS` and
  `/DEBUG:FULL` everywhere (PDBs in every configuration), `/RTC1` dropped (incompatible with ASan). Warnings are not errors, as on Linux. ASan: MSVC yes;
  clang-cl not wired (its runtime libraries must be named on the `lld-link` line), TSan none.
- **Manifest:** `cmake/engine.manifest`, added at the end of configure to every executable target (a deferred call; CMake merges it with the `mt.exe` the toolchain
  found, because `/MANIFEST:EMBED` made `link.exe` look for `mt.exe` on `PATH`). `windows_manifest` shows code page 65001 and per-monitor-v2 DPI.
- **`windows.h` wrapper:** the clashing names were measured by intersecting every macro `windows.h` defines (21 489) with the engine's identifiers: `LoadLibrary`,
  `CreateDirectory`, `SetCurrentDirectory`, `CreateSemaphore`, `near`, `far`, `small`; plus 28 defensive names. `os/windows/win32.h` includes `windows.h`
  between `unity/windows_enable.h` and `windows_disable.h`, which restore those names at once; `interface` is left alone because the COM headers need it.
  `windows_header_check` (every foundation header, then `win32.h`, then uses of the names) compiles; `windows_header_planted` (bare `windows.h`) fails with
  `'LoadLibraryW': is not a member of 'vse::os'` (cl) / `no member named 'LoadLibraryW'` (clang-cl), the reason the wrapper exists.
- **Shared-code portability:** `ENGINE_PRINTF_FORMAT` replaces the GCC format attribute in the ten files and `ENGINE_TRAP` the `__builtin_trap` of `jobs/job.h`
  (both in `debug/compiler.h`). Two silent MSVC problems found and fixed: **`[[msvc::noinline]]` in `ENGINE_NOINLINE_LAMBDA`'s position is ignored by MSVC** (C4649;
  the lambda was inlined, checked in the generated assembly; `__declspec(noinline)` is honored), and **cl and clang-cl ignore the standard `[[no_unique_address]]`**,
  which made `UniquePtr` twice the size of a pointer (new `ENGINE_NO_UNIQUE_ADDRESS`; the header check asserts the size). The unused local in `vk_memory.cpp` is gone.
  Not added: the bit-scan, `__forceinline` and `__restrict` spellings the plan listed (nothing uses those builtins). `int128.h` keeps the portable path on MSVC
  without `_umul128`/`_addcarry_u64` (not measured; filed as a deferred optimization). `_Printf_format_string_` not used.
- **`long`:** kept. `os::Write`/`Read`/`WriteAllWithTimeout` return `long`, which is 32 bits on Windows; `WriteFile` takes a 32-bit length anyway, so the Windows
  backend caps each call at 2^31-1 bytes and reports a short write, which every caller already handles. Day 149 documents it in `os.h`.
- **`clang-cl` and `__int128`:** the configuration links `clang_rt.builtins-x86_64.lib` (highest version under `lib/clang/`). Not run: `tests/types` under either
  compiler, because it links `foundation_base`, whose `os/windows/` backend is Day 149 (task left open).
- **Platform-qualified `flavors` lines:** `flavors 13 windows: plain` in `tests/phases.txt`; `test_phase_labels` takes `-DPLATFORM` and checks the line for that
  platform; the fixture `platform_only` is checked as Linux and as Windows. `tests/phase_labels` is not in the Windows build until Day 150 (every other phase's tests
  are absent there, so its other checks would fail today). Not run yet: these tests run on Linux.
- **Runs (Windows only; `scripts/test-phase.sh` does not run on Windows until Day 150, so these are plain `ctest` results):** `ctest --preset windows-msvc`: 5 of 5
  passed (`windows_header_check`, `windows_header_planted`, `windows_manifest`, `windows_toolchain_discovery_msvc`, `_clang_cl`); `ctest --preset windows-clang-cl`:
  5 of 5. Builds with no warning in both presets, unity on and off (`-DENGINE_UNITY_BUILD=OFF` in `build-windows-msvc-nounity`, `build-windows-clang-cl-nounity`).
- **Not done, needs the Linux machine:** the Linux CMake version check for the 3.21 minimum; both Linux unity modes; `scripts/test-phase.sh --affected` (a
  `CMakeLists.txt` and foundation headers changed: everything) with asan and tsan; the new phase-label self-tests; the reference regeneration for `compiler.h`'s
  three new macros, `docgen lint` and `ctest -L docs` (docgen does not build on Windows until Day 150). Filed in `KNOWN-ISSUES.md`.

## Post-implementation code review

`/code-review high` over the day's uncommitted diff: **10 findings, 9 fixed, 1 deferred.**

Fixed: (1) the build-wide `/utf-8` and `/Zi /FS` would have reached `rc.exe` (sentry's resource file, Day 154): now C and C++ only. (2) Stripping `/EHsc` for
every target left tools that need exceptions without them (`shader_mint` with nlohmann/json): `ENGINE_EXCEPTIONS_COMPILE_OPTIONS`, given to Catch2 now and to
`shader_mint` on Day 150 (task added). (3) The Windows definitions and conformance flags reached vendored code: now applied after `third_party/`. (4) The Developer
Command Prompt check ignored a different SDK in the prompt's `INCLUDE`/`LIB`: it now compares `WindowsSdkDir` and `WindowsSDKVersion` too. (5) The builtins lookup
failed obscurely on an empty glob and sorted versions as text: checked and sorted naturally. (6) The whole Vulkan SDK `Include/` (with slang, shaderc, dxc) was a
public include: only the Vulkan C headers are copied and exposed. (7) `ENGINE_TRAP` was not seen as terminating on MSVC (`__ud2` is a plain function):
`(__ud2(), __assume(0))`. (9) An explicit `ENGINE_VS_INSTALL_DIR` under another prompt still failed: the override now wins with a warning. (10) `compiler.h`'s
comment still said it served the debugging facilities only. A first attempt at (3) broke the build (a generator expression holding `;` split when the list was
expanded); written one per flag. The prompt behaviors were checked by hand inside `vcvars64` (above); the rest are covered by the Windows tests and builds.

Deferred: (8) the CLAUDE.md docs rule for `compiler.h`'s new macros (reference regeneration, `docgen lint`, `ctest -L docs`): the macros carry `///` comments and the
guide (`foundation-debugging.md`, "Compiler-specific constructs") is written, but docgen runs only on Linux until Day 150; done on the Linux machine.
