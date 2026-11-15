# Phase 21 / Day 283 — Design Spec, Inventory, Toolchain & Phase Setup

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 282.

Write the spec that confirms or amends the phase file's twelve decisions, inventory every Linux-specific file against its macOS counterpart, record the
Mac's toolchain in `BUILD.md`, and set the phase up as a side phase. Spikes are throwaway programs in the scratch directory.

## Tasks

- [x] **Memory link on the Mac:** `~/.claude/projects/-Users-user-src-re/memory` is an empty directory, not the link to `claude-memory/`; run
      `scripts/link-claude-memory.sh` and check a memory note reads through the link. If the script assumes GNU `ln`/`readlink` options, fix it for BSD
      userland (shared script: file the Linux check).
- [x] **Side-phase setup:** `tests/phases.txt`'s phase list names "26 macOS port" and gets `flavors 26 plain,asan,tsan`; `CURRENT_PHASE` stays as it
      is. Add the side-phase rule to CLAUDE.md ("Tests run by phase": a side phase listed in `SIDE_PHASES` is added to `--affected` runs on the machine
      doing it; its days quote their own `ran X of Y` line) **marked as proposed until the user confirms it**; `docs/guide/building-and-testing.md`
      names it. The tooling change itself is Day 286.
- [x] **Case-insensitive checkout:** list paths in the tree that differ only in case (`git ls-files | sort -f | uniq -di`), and grep the gates and tests
      for assumptions that the file system is case-sensitive (fixtures named `Foo`/`foo`, tests that create both). Record each; none may stay.
- [x] **Prerequisites** (phase file list): install `slangc` v2026.18.2 for macOS and check it mints byte-identical SPIR-V for three shaders against the
      committed `*.spv.inc`; record every version (Command Line Tools, Apple clang, SDK version from `xcrun --show-sdk-version`, CMake, Ninja, Python, `lldb`,
      `dsymutil`). Write the **macOS section of `BUILD.md`** (required, for checks, optional; MacPorts install commands; kitty's `slangc` shadowing
      warning; the Day 282 decision on Xcode).
- [x] **Inventory** (a table in the spec: file, what is Linux-specific, macOS counterpart, shared `posix/` or `macos/`, the port day): every file under
      `os/linux/` (with `debug/`), `io/linux/`, `settings/linux/`, `engine/platform/linux/`, every `#if ENGINE_PLATFORM_LINUX` in shared code
      (`io/path.h`, `debug/crash_event_platform.h`, `debug/crash_handler_sentry.cpp`, `platform/entry_point.h`, `tools/crash_extract/resolve.cpp`,
      `tools/docgen/src/samples.cpp`), every CMake `if(LINUX)`/`CMAKE_SYSTEM_NAME` branch, every script under `scripts/`, every test that is Linux-only, and
      each `KNOWN-ISSUES.md` line that names Phase 21.
- [x] **Spike A — the build.** Remove the Darwin stop locally (not committed) and configure with Apple clang and Ninja; record every failure class with
      one example (CMake branches, missing platform sources, Apple header macros, warnings Apple clang raises that Linux clang does not, link flags such
      as `--build-id` and `-Wl,--gc-sections`). Time a full unity build of `foundation_base` on this 4-core machine.
- [x] **Spike B — Objective-C++ under the rules.** A throwaway `.mm` that includes `<AppKit/AppKit.h>` and `<Metal/Metal.h>`, compiled with
      `-fobjc-arc -Wall -Wextra -Werror` in a unity group with a C++ file of ours: list the macros that leak (`check`, `verify`, `require`, `nil`, `Nil`, `YES`,
      `NO`, `BOOL`, MacTypes `Point`/`Rect`/`Handle`/`Size`) and confirm `apple_enable.h`/`apple_disable.h` with `push_macro` contain them; confirm the
      opaque-pointer pattern (a C++ class owning an `id<MTLDevice>` through ARC in a `.mm`, the neutral header unchanged) leaks nothing under ASan.
- [x] **Spike C — the window on the main thread.** A throwaway program: `NSApplication` without a bundle, a window with a `CAMetalLayer` view, a polled
      event loop (`nextEventMatchingMask:untilDate:distantPast`) driven from our own loop, clearing to a color each frame through Metal; confirm it shows,
      takes keyboard focus when started from a terminal (`activateIgnoringOtherApps`), resizes and closes, and measure the frame time.
- [x] **The spec** `docs/superpowers/specs/2026-11-NN-macos-port-design.md`: each phase-file decision confirmed or amended with the evidence; the
      inventory; the `posix/` split; the side-phase rule; the decisions the spikes changed are written back into the phase file.
- [ ] **Documentation:** the spec is linked from `docs/README.md`; the phase file's day list is current; `ctest -L docs` passes (on Linux until Day 286).
- [x] No code review (no product code beyond the setup). Update `STATUS.md` (a "Phase 21 (side)" line), `KNOWN-ISSUES.md` (the Linux checks for the
      shared files touched); commit and push.

**Done:** memory is linked on the Mac; the phase map knows phase 21 and the proposed side-phase rule is written; the checkout is free of case-only name
clashes; `BUILD.md` has a macOS section with every version recorded; the spec with the inventory and the three spike results is committed and linked.

## Result (2026-11-11, on the Mac)

Evidence and the full inventory: `docs/superpowers/specs/2026-11-11-macos-port-design.md` (linked from `docs/README.md`).

- **Memory:** `scripts/link-claude-memory.sh` ran unchanged (BSD-clean): the empty `memory` directory was moved aside (and the empty backup removed),
  the link points at `claude-memory/`, and `MEMORY.md` reads through it.
- **Side phase:** `tests/phases.txt`'s comment names phase 21 as a side phase and its label; the rule is in CLAUDE.md ("Tests run by phase") and in
  `docs/guide/building-and-testing.md` ("Side phases"), both **marked as proposed** until the user confirms. **Changed from the plan:** the
  `flavors 26 macos: plain,asan,tsan` line is not added today: `test_phase_labels` refuses a `flavors` line for a phase with no tests in the map
  (`tests/phase_labels/check.cmake:110`), so it comes with the first phase-27 test (Day 284 task).
- **Case:** no path or directory in the tree differs from another only in case; no gate or test assumes a case-sensitive file system (the one test
  that cares already avoids it). The volume is APFS, case-insensitive.
- **Prerequisites:** `slangc` v2026.18.2 installed at `~/tools/slang-2026.18.2` (release archive, SHA-256 recorded in the spec); it mints SPIR-V
  word-identical to the committed `triangle.vert`, `gbuffer.vert`, `lighting.frag` and `cube.frag`. Versions: Command Line Tools 16.4, Apple clang 17.0.0,
  SDK 15.5, CMake 4.4.4, Ninja 1.13.2, Python 3.14.10, lldb 1700.0.9.502, Git 2.39.5, MacPorts 2.12.7, bash 3.2.57. `BUILD.md` has its macOS section.
  Found: `shaders/CMakeLists.txt:57` would pick kitty's `slangc` from `PATH` (Day 284 task).
- **Inventory** (spec section 8; surveyed by a subagent and spot-checked): 18 per-platform foundation and jobs files (7 move to `posix/`, 10 get a
  `macos/` counterpart, the Vulkan loader name needs none), 45 Linux-only platform files, 7 platform conditions in `engine/` plus 2 unconditional
  Vulkan dependencies, 4 CMake stops for Darwin, 6 CMake `else()` branches that mean Linux, 5 of 7 shell scripts to adapt (no bash-4 feature
  anywhere), 12 Linux-only debug tests, 21 `KNOWN-ISSUES.md` lines. **Corrections to the plan written into the day files:** `engine/jobs` has a platform
  file (Day 289), `ENOTSUP != EOPNOTSUPP` on macOS (Day 287), no `pipe2` (Day 291), `--wrap=malloc` is GNU ld only (Day 290), `/proc` reads in
  `os.cpp` (Day 285).
- **Spike A (the build):** with the Darwin stop removed locally and the **Linux** `os/` and `io/` sources unchanged, the base and file layers and the
  four base test suites built in plain, asan and tsan with **no new warning**, unity and non-unity (49 s and 67 s for everything; `foundation_base`
  alone 1.3 s); `ctest -j4`: **212 of 215 passed**, the three failures being the spike's own stand-in for `ParseDoubleExact` (caught by the existing
  test). Failure classes: the four Darwin stops (libbacktrace's with stale phase numbers), Vulkan headers in the base layer (`gpu_driver_probe.h`),
  `st_mtim`, `std::from_chars` for `double` deleted in Apple's libc++ (moved to Day 284, since `tests/cstr` cannot compile without it), floating
  `to_chars` needing macOS 13.3 (deployment target amended), and sentry not configured (expected). ASan aborts when `detect_leaks=1` is asked for;
  `detect_leaks=0` is accepted. All spike edits were reverted (`git status` clean before today's edits).
- **Spike B (Objective-C++):** all 243 neutral engine headers compile with AppKit, Metal and QuartzCore before them and after them under
  `-fobjc-arc -Wall -Wextra -Werror`: no clash, so the `apple_enable.h`/`apple_disable.h` pair is **not** added up front (decision 3 amended). The
  ARC opaque-pointer pattern: 0 leaks (`leaks --atExit`), clean under ASan+UBSan and TSan.
- **Spike C (the window):** `NSApplication` without a bundle, a `CAMetalLayer` view, our own polled loop: the window shows at Retina scale 2.0, the
  drawable follows a resize (1800x1064 for 900x532 points), the close button's path reaches `windowShouldClose:` and the program decides. **Not
  checked:** activation and key focus from a terminal, and frame pacing: the session was locked with the display asleep (`CGSSessionScreenIsLocked`
  1, `CGDisplayIsAsleep` 1), so the app never became active and frames were not paced (0.92 ms mean). The same diagnosis confirmed the session-state
  design (decision 7). Rerun with the user present before Day 295 (`KNOWN-ISSUES.md`).
- **Not run here:** `ctest -L docs` (the docs build runs on the Mac from Day 286) and the Linux `scripts/test-phase.sh --affected` (the phase map's
  comment changed, so the whole plain suite runs there): both filed in `KNOWN-ISSUES.md` for the next Linux session.
