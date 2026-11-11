# Phase 26 / Day 284 — CMake, Apple Clang and Objective-C++ on macOS

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 283.

Make the tree configure and build on macOS one layer at a time, as Phase 13 Day 148 did on Windows: `foundation_base` and the layer-0 tests first;
higher layers stay unconfigured on macOS until their day (each listed in the spec's inventory).

## Tasks

- [ ] **Remove the stop** in the root `CMakeLists.txt` (`CMAKE_SYSTEM_NAME STREQUAL "Darwin"`) and in `engine/foundation/CMakeLists.txt`; replace it
      with a macOS branch that configures only the layers already ported and prints which are not yet (the list shrinks day by day and is gone on
      Day 310).
- [ ] **Preset `macos-clang`** in `CMakePresets.json` (Ninja, tree `build-macos`, `CMAKE_OSX_DEPLOYMENT_TARGET=13.0`, `CMAKE_OSX_ARCHITECTURES=x86_64`,
      build and test presets; **deployment target 13.3**, amended on Day 283: floating `std::to_chars` needs it); the configure finds Apple clang through `xcrun` (never MacPorts' clang first on `PATH`) and fails with a clear message
      without the Command Line Tools.
- [ ] **Compiler options** (`cmake/EngineCompilerOptions.cmake`): the warning set unchanged; `ENGINE_PRODUCTION_COMPILE_OPTIONS` and the link options
      get an Apple branch (`-Wl,-dead_strip` instead of `--gc-sections`, no `--build-id`: the linker writes `LC_UUID`); `OBJCXX` enabled with
      `-fobjc-arc` and the same warnings for `.mm` files; fix (not silence) every warning Apple clang raises that Linux clang does not.
- [ ] **Unity builds with Apple headers:** `.mm` files form their own unity groups (CMake merges per language).
      **Amended on Day 283:** Spike B found no clash between Apple's framework headers and any engine header in either order, so no
      `apple_enable.h` / `apple_disable.h` pair is added up front; per CLAUDE.md ("Unity builds") a pair is added the day a real conflict appears.
      `__ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES` is already 0 in this SDK; define it to 0 for our targets anyway so a future SDK cannot
      bring back `check`/`verify`/`require`.
- [ ] **Sanitized targets:** the `*_asan` (ASan + UBSan) and `*_tsan` targets build with Apple clang; `detect_leaks` is unsupported there, so the ASan
      options file drops it on macOS with a comment naming `KNOWN-ISSUES.md`; record whether `-fsanitize=undefined` needs `-fno-sanitize=function` or
      similar for Objective-C (fix the code if it reports).
- [ ] **`build_defs.h` check:** `ENGINE_PLATFORM_MACOS`, `ENGINE_ARCH_X64`, `ENGINE_COMPILER_CLANG`, `ENGINE_SANITIZER_*` evaluate as expected on Apple
      clang (a test prints and checks them); add `ENGINE_COMPILER_APPLE_CLANG` only if Spike A found a difference that needs it (then document it in
      `docs/guide/foundation-types.md`).
- [ ] **Layer 0 builds and runs:** `foundation_base` (lang, sync, cstr, types, `int128` with native `__int128`) and `tests/lang`, `tests/sync`,
      `tests/cstr`, `tests/types` build and pass with plain `ctest` (the test script runs on macOS from Day 286); the `os/` layer links against a
      temporary stub only if the spec's order requires it (otherwise Day 285 fills it).
- [ ] **`ParseDoubleExact` on Apple's libc++** (`cstr/number.h`, a layer-0 wrapper, so it is needed for `tests/cstr` to compile at all; Day 283 Spike A): `std::from_chars` for `double` is a deleted function there. A
      macOS path behind the same contract: check the form first (no leading `+` or space, no hex: `0x10` reads `0` and stops at the `x`), then
      `strtod_l` with a C locale made once; `tests/cstr/cstr_test.cpp` (`ParseDoubleExact reads plain decimals only`) is the oracle, and a test
      compares the two paths on Linux over a corpus (shortest round-trip strings, subnormals, `nan`, `inf`, overlong inputs).
- [ ] **No Vulkan in the base layer on macOS** (Day 283, design spec decision 8): `os/gpu_driver_probe.cpp` and `vulkan_loader_name.cpp` are not built
      on macOS and `gpu_driver_loading_test.cpp` is not configured there; the probe's header documents it.
- [ ] **The other stops and lookups Spike A found:** `third_party/libbacktrace/CMakeLists.txt:13`'s stale message ("macOS is Phase 9 and Windows Phase 8")
      names Phase 26 Day 290 until that day builds it; `shaders/CMakeLists.txt:57` gets `~/tools/slang-2026.18.2/bin` as a hint and refuses a `slangc`
      whose `-v` is not a Slang version (kitty's `slangc` comes first on `PATH` on this Mac).
- [ ] **Phase map:** the first phase-26 test (the `build_defs.h` check below) adds its `tests/phases.txt` line and `flavors 26 macos: plain,asan,tsan`
      (`test_phase_labels` refuses a `flavors` line for a phase without tests, `tests/phase_labels/check.cmake:110`).
- [ ] **CMake gates:** `module_deps_check` and `std_usage_check` pass when run by hand on macOS.
- [ ] **Both unity modes:** `cmake --preset macos-clang` and `-DENGINE_UNITY_BUILD=OFF` build clean; record full-build times (8 GB, 4 cores).
- [ ] **Documentation:** `docs/guide/building-and-testing.md` gets a short "Building on macOS" section (the full guide is Day 311); `STATUS.md` names the
      preset.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the CMake branches (nothing Linux-only now skipped on Linux), the probe and
      `slangc` changes leaving Linux and Windows unchanged.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md` (the Linux and Windows checks for the shared CMake files); commit and push.

**Done:** on the Mac, `cmake --preset macos-clang` configures and builds the base layer with no warning in unity and non-unity modes, the layer-0 tests
pass in plain, asan and tsan (quoted as plain ctest results), `ParseDoubleExact` passes its contract tests on macOS, and the shared-file checks are filed for the other
machines.
