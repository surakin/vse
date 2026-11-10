# Phase 26 / Day 268 — CMake, Apple Clang and Objective-C++ on macOS

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 267.

Make the tree configure and build on macOS one layer at a time, as Phase 13 Day 148 did on Windows: `foundation_base` and the layer-0 tests first;
higher layers stay unconfigured on macOS until their day (each listed in the spec's inventory).

## Tasks

- [ ] **Remove the stop** in the root `CMakeLists.txt` (`CMAKE_SYSTEM_NAME STREQUAL "Darwin"`) and in `engine/foundation/CMakeLists.txt`; replace it
      with a macOS branch that configures only the layers already ported and prints which are not yet (the list shrinks day by day and is gone on
      Day 294).
- [ ] **Preset `macos-clang`** in `CMakePresets.json` (Ninja, tree `build-macos`, `CMAKE_OSX_DEPLOYMENT_TARGET=13.0`, `CMAKE_OSX_ARCHITECTURES=x86_64`,
      build and test presets); the configure finds Apple clang through `xcrun` (never MacPorts' clang first on `PATH`) and fails with a clear message
      without the Command Line Tools.
- [ ] **Compiler options** (`cmake/EngineCompilerOptions.cmake`): the warning set unchanged; `ENGINE_PRODUCTION_COMPILE_OPTIONS` and the link options
      get an Apple branch (`-Wl,-dead_strip` instead of `--gc-sections`, no `--build-id`: the linker writes `LC_UUID`); `OBJCXX` enabled with
      `-fobjc-arc` and the same warnings for `.mm` files; fix (not silence) every warning Apple clang raises that Linux clang does not.
- [ ] **Unity builds with Apple headers:** add `engine/foundation/unity/apple_enable.h` / `apple_disable.h` (the macro list from Spike B, saved with
      `push_macro`), `__ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES=0` as a compile definition for our targets; `.mm` files form their own unity
      groups (CMake merges per language). Document the pair in `docs/guide/building-and-testing.md` ("Unity builds").
- [ ] **Sanitized targets:** the `*_asan` (ASan + UBSan) and `*_tsan` targets build with Apple clang; `detect_leaks` is unsupported there, so the ASan
      options file drops it on macOS with a comment naming `KNOWN-ISSUES.md`; record whether `-fsanitize=undefined` needs `-fno-sanitize=function` or
      similar for Objective-C (fix the code if it reports).
- [ ] **`build_defs.h` check:** `ENGINE_PLATFORM_MACOS`, `ENGINE_ARCH_X64`, `ENGINE_COMPILER_CLANG`, `ENGINE_SANITIZER_*` evaluate as expected on Apple
      clang (a test prints and checks them); add `ENGINE_COMPILER_APPLE_CLANG` only if Spike A found a difference that needs it (then document it in
      `docs/guide/foundation-types.md`).
- [ ] **Layer 0 builds and runs:** `foundation_base` (lang, sync, cstr, types, `int128` with native `__int128`) and `tests/lang`, `tests/sync`,
      `tests/cstr`, `tests/types` build and pass with plain `ctest` (the test script runs on macOS from Day 270); the `os/` layer links against a
      temporary stub only if the spec's order requires it (otherwise Day 269 fills it).
- [ ] **CMake gates:** `module_deps_check`'s `modules.txt` and `std_usage_check`'s allow list accept the new `unity/apple_*` headers; both gates pass
      when run by hand on macOS.
- [ ] **Both unity modes:** `cmake --preset macos-clang` and `-DENGINE_UNITY_BUILD=OFF` build clean; record full-build times (8 GB, 4 cores).
- [ ] **Documentation:** `docs/guide/building-and-testing.md` gets a short "Building on macOS" section (the full guide is Day 295); `STATUS.md` names the
      preset.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the CMake branches (nothing Linux-only now skipped on Linux), the macro wrapper's
      completeness.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md` (the Linux and Windows checks for the shared CMake files); commit and push.

**Done:** on the Mac, `cmake --preset macos-clang` configures and builds the base layer with no warning in unity and non-unity modes, the layer-0 tests
pass in plain, asan and tsan (quoted as plain ctest results), the Apple macro wrapper is documented, and the shared-file checks are filed for the other
machines.
