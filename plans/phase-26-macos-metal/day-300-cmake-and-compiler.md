# Phase 26 / Day 300 — CMake, Apple Clang and Objective-C++ on macOS

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 299.

Make the tree configure and build on macOS one layer at a time, as Phase 13 Day 148 did on Windows: `foundation_base` and the layer-0 tests first;
higher layers stay unconfigured on macOS until their day (each listed in the spec's inventory).

## Tasks

- [x] **Remove the stop** in the root `CMakeLists.txt` (`CMAKE_SYSTEM_NAME STREQUAL "Darwin"`) and in `engine/foundation/CMakeLists.txt`; replace it
      with a macOS branch that configures only the layers already ported and prints which are not yet (the list shrinks day by day and is gone on
      Day 326).
- [x] **Preset `macos-clang`** in `CMakePresets.json` (Ninja, tree `build-macos`, `CMAKE_OSX_DEPLOYMENT_TARGET=13.0`, `CMAKE_OSX_ARCHITECTURES=x86_64`,
      build and test presets; **deployment target 13.3**, amended on Day 299: floating `std::to_chars` needs it); the configure finds Apple clang through `xcrun` (never MacPorts' clang first on `PATH`) and fails with a clear message
      without the Command Line Tools.
- [x] **Compiler options** (`cmake/EngineCompilerOptions.cmake`): the warning set unchanged; `ENGINE_PRODUCTION_COMPILE_OPTIONS` and the link options
      get an Apple branch (`-Wl,-dead_strip` instead of `--gc-sections`, no `--build-id`: the linker writes `LC_UUID`); `OBJCXX` enabled with
      `-fobjc-arc` and the same warnings for `.mm` files; fix (not silence) every warning Apple clang raises that Linux clang does not.
- [x] **Unity builds with Apple headers:** `.mm` files form their own unity groups (CMake merges per language).
      **Amended on Day 299:** Spike B found no clash between Apple's framework headers and any engine header in either order, so no
      `apple_enable.h` / `apple_disable.h` pair is added up front; per CLAUDE.md ("Unity builds") a pair is added the day a real conflict appears.
      `__ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES` is already 0 in this SDK; define it to 0 for our targets anyway so a future SDK cannot
      bring back `check`/`verify`/`require`.
- [x] **Sanitized targets:** the `*_asan` (ASan + UBSan) and `*_tsan` targets build with Apple clang; `detect_leaks` is unsupported there, so the ASan
      options file drops it on macOS with a comment naming `KNOWN-ISSUES.md`; record whether `-fsanitize=undefined` needs `-fno-sanitize=function` or
      similar for Objective-C (fix the code if it reports).
- [x] **`build_defs.h` check:** `ENGINE_PLATFORM_MACOS`, `ENGINE_ARCH_X64`, `ENGINE_COMPILER_CLANG`, `ENGINE_SANITIZER_*` evaluate as expected on Apple
      clang (a test prints and checks them); add `ENGINE_COMPILER_APPLE_CLANG` only if Spike A found a difference that needs it (then document it in
      `docs/guide/foundation-types.md`).
- [x] **Layer 0 builds and runs:** `foundation_base` (lang, sync, cstr, types, `int128` with native `__int128`) and `tests/lang`, `tests/sync`,
      `tests/cstr`, `tests/types` build and pass with plain `ctest` (the test script runs on macOS from Day 302); the `os/` layer links against a
      temporary stub only if the spec's order requires it (otherwise Day 301 fills it).
- [x] **`ParseDoubleExact` on Apple's libc++** (`cstr/number.h`, a layer-0 wrapper, so it is needed for `tests/cstr` to compile at all; Day 299 Spike A): `std::from_chars` for `double` is a deleted function there. A
      macOS path behind the same contract: check the form first (no leading `+` or space, no hex: `0x10` reads `0` and stops at the `x`), then
      `strtod_l` with a C locale made once; `tests/cstr/cstr_test.cpp` (`ParseDoubleExact reads plain decimals only`) is the oracle, and a test
      compares the two paths on Linux over a corpus (shortest round-trip strings, subnormals, `nan`, `inf`, overlong inputs).
- [x] **No Vulkan in the base layer on macOS** (Day 299, design spec decision 8): `os/gpu_driver_probe.cpp` and `vulkan_loader_name.cpp` are not built
      on macOS and `gpu_driver_loading_test.cpp` is not configured there; the probe's header documents it.
- [x] **The other stops and lookups Spike A found:** `third_party/libbacktrace/CMakeLists.txt:13`'s stale message ("macOS is Phase 9 and Windows Phase 8")
      names Phase 26 Day 306 until that day builds it; `shaders/CMakeLists.txt:57` gets `~/tools/slang-2026.18.2/bin` as a hint and refuses a `slangc`
      whose `-v` is not a Slang version (kitty's `slangc` comes first on `PATH` on this Mac).
- [x] **Phase map:** the first phase-26 test (the `build_defs.h` check below) adds its `tests/phases.txt` line and `flavors 26 macos: plain,asan,tsan`
      (`test_phase_labels` refuses a `flavors` line for a phase without tests, `tests/phase_labels/check.cmake:110`).
- [x] **CMake gates:** `module_deps_check` and `std_usage_check` pass when run by hand on macOS.
- [x] **Both unity modes:** `cmake --preset macos-clang` and `-DENGINE_UNITY_BUILD=OFF` build clean; record full-build times (8 GB, 4 cores).
- [x] **Documentation:** `docs/guide/building-and-testing.md` gets a short "Building on macOS" section (the full guide is Day 327); `STATUS.md` names the
      preset.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the CMake branches (nothing Linux-only now skipped on Linux), the probe and
      `slangc` changes leaving Linux and Windows unchanged.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md` (the Linux and Windows checks for the shared CMake files); commit and push.

**Done:** on the Mac, `cmake --preset macos-clang` configures and builds the base layer with no warning in unity and non-unity modes, the layer-0 tests
pass in plain, asan and tsan (quoted as plain ctest results), `ParseDoubleExact` passes its contract tests on macOS, and the shared-file checks are filed for the other
machines.

## Result (2026-11-11, on the Mac)

- **Configure:** the root `CMakeLists.txt` takes Apple clang through `xcrun` before `project()` (only on a Darwin host with no compiler, toolchain or
  `CXX`/`CC` given; it stops with an install hint without the Command Line Tools), sets the deployment target to 13.3 (not for a cross build),
  enables `OBJCXX`, maps x86-64 Darwin to `ENGINE_PLATFORM macos` (arm64 stops, naming Day 326), and has a macOS block that adds the base layer,
  `tests/types`, `lang`, `sync`, `cstr`, the new `tests/macos` and `tests/phase_labels`, printing what is not ported yet. FreeType and SQLite are
  not added on macOS until their layers (review finding 7). `engine/foundation/CMakeLists.txt` builds `foundation_base` in plain, asan and tsan
  on macOS, **temporarily with the Linux `os.cpp` and `session.cpp`** (Day 301 replaces them; `KNOWN-ISSUES.md`). Preset `macos-clang`
  (`build-macos`). Picked: `/Library/Developer/CommandLineTools/usr/bin/clang++`.
- **Compiler options:** the GCC/Clang set applies unchanged; the warnings also cover `OBJCXX`; `ENGINE_MACOS_COMPILE_OPTIONS` (ARC) and
  `ENGINE_MACOS_DEFINITIONS` (`__ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES=0`). The production options (no exceptions, RTTI, stack
  protector) now reach Objective-C++ in all nine engine libraries that apply them (review finding 1). **No `-dead_strip`:** the tree has no
  `--gc-sections` to replace (the task's premise was wrong); `--build-id` was already Linux-only. Apple clang raised **no warning** that Linux clang
  does not, in unity and non-unity builds.
- **Sanitizers:** the asan (ASan + UBSan) and tsan flavors build and pass. No options file asks for `detect_leaks=1` (the only setting in the tree is the
  death test's `detect_leaks=0`, which macOS accepts), so there was nothing to drop; the comment in `EngineCompilerOptions.cmake` records the rule.
  UBSan on Objective-C++ is not exercised yet (no `.mm` in a sanitized target; `KNOWN-ISSUES.md`).
- **`build_defs.h`:** `tests/macos/build_defs_macos_test.cpp` (the first phase-26 test, three flavors) checks platform, processor, compiler,
  `ENGINE_HAS_BUILTIN` and each flavor's sanitizer macros; no `ENGINE_COMPILER_APPLE_CLANG` is needed.
- **`ParseDoubleExact` on Apple's libc++:** `detail::ParseDoubleWithoutFromChars` (`cstr/number.cpp`, POSIX systems) reads `from_chars`' grammar itself and
  converts a normalized copy (780 significant digits and a sticky digit, exponent saturated at 10^15) with `strtod_l` in the C locale.
  `tests/cstr/parse_double_test.cpp`: 35 contract cases (ends, signs, `inf`/`infinity`/`nan(...)`, hex, subnormal and overflow edges, ties to even),
  long-digit cases, a 10-million-digit case, and 200,000 random doubles read back from their shortest and 40-digit forms, on every platform; on Linux
  it also compares the two parsers over the cases and 300,000 random strings (never run yet: `KNOWN-ISSUES.md`). Two of its cases were shown to fail
  without their fixes: the sticky digit kept in place (`1.` + 800 zeros + `1` read as 1.1) and the exponent saturation (read as 0).
- **No Vulkan in the base layer:** `os/gpu_driver_probe.cpp` moved into the Linux and Windows platform lists; the header documents that macOS does not
  build it. The test's exclusion lands with `tests/foundation` on Day 301 (task added there).
- **Other stops and lookups:** libbacktrace's message and `VENDORED.md` corrected (they named Phases 9, 8 and 6 for Phase 26 Day 306, Phase 13 Day 154
  and Phase 7 Day 91). `shaders/CMakeLists.txt` looks in `~/tools/slang-2026.18.2` before `PATH` and refuses a `slangc` whose `-v` is not a Slang version,
  reading it with no input, a 10 s limit and either line ending; checked by a script against kitty's `slangc` (refused), the real one, a Linux-style
  `v2026.18.2` with a second line, a CRLF answer, one that reads its input and one that hangs (refused after 10 s).
- **Phase map:** `tests/macos/` and `testopt:macos_*` are phase 26; a macOS flavor block (`flavors <n> macos: ...`: 6 plain, 10, 16 and 26 in three
  flavors, the rest `none`); `test_phase_labels` passes on macOS.
- **Header checks** (`tests/macos`): every neutral foundation header compiles beside AppKit, Metal and QuartzCore in both orders; `macos_header_planted`
  (the old AssertMacros names on) and `macos_production_options_planted` (a `try` in an engine library's `.mm`) must fail, each matched on its message.
- **Gates by hand:** `module_deps_check` (259 edges, 0 problems), `std_usage_check` (948 files), `file_access_check` (935), `config_access_check` (575),
  `platform_neutral_check` (clean).
- **Runs (plain `ctest`, since `scripts/test-phase.sh` runs on macOS from Day 302):** `ctest --preset macos-clang -j4`: **246 of 246 passed** (unity);
  the same 246 of 246 in `-DENGINE_UNITY_BUILD=OFF`. Full builds on this machine: 45 s unity, 62 s non-unity (Ninja, 8 jobs, 8 GB).
- **Not done here:** `docs/reference/` regeneration and `docgen lint` for `cstr/number.h` and `os/gpu_driver_probe.h`, and `ctest -L docs`: `docgen` does
  not build on macOS before Day 302; filed for the Linux machine with the other shared-file runs (`KNOWN-ISSUES.md`). This is an exception to CLAUDE.md's
  "Public API is documented before a day is done" that the review named (finding 3), taken because the tool cannot run on this machine yet.

## Post-implementation code review

`/code-review high` over the day's working tree: **10 findings: 8 fixed, 2 fixed in part (with the rest declined), none ignored.**

Fixed: (1) the production options were scoped to `CXX` only, so an engine `.mm` would have built with exceptions and RTTI: now `CXX,OBJCXX` in all nine
libraries, with `macos_production_options_planted` (shown to fail before the fix); (2) the exponent stopped growing past 1,000,000 while `point` did not, so
a long number with a long exponent came out wrong but `ok` (the review's example was one digit short: it takes an 8-digit exponent, `0.` + 10,000,000
zeros + `1e10000001`, which read as 0 instead of 1.0): saturated at 10^15, with that case as a test, shown to fail before the fix; (4) `BUILD.md`'s status
and the `ENGINE_SLANGC` advice updated; (5) the `slangc -v` probe gets no input and a 10 s limit; (6) the version is cut at the first CR or LF, so a CRLF
answer on Windows is not refused; (7) FreeType and SQLite are no longer built on macOS before their layers; (9) the deployment target is not set for a
cross build (a later iOS toolchain); (10) `WordLength` replaced by `CStrLength`.
In part: (3) `docs/reference/` is not regenerated in this commit: `docgen` cannot build on the Mac until Day 302, so the regeneration is filed for the next
Linux session and the day's documentation is complete only when it lands; (8) the declaration of `detail::ParseDoubleWithoutFromChars` is now guarded like
its definition (no link error on Windows); declined: moving the Linux copy out of `foundation_base` into the test, because the comparison must test the
code the macOS build compiles, not a copy built for the test (about 1 KB in Linux binaries). Declined: sharing `Lower` with `settings/desc.cpp` (one
line, and a shared ASCII helper in `cstr` would be new public API for no caller beyond these two).
