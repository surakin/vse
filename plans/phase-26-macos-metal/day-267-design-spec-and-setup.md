# Phase 26 / Day 267 — Design Spec, Inventory, Toolchain & Phase Setup

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 266.

Write the spec that confirms or amends the phase file's twelve decisions, inventory every Linux-specific file against its macOS counterpart, record the
Mac's toolchain in `BUILD.md`, and set the phase up as a side phase. Spikes are throwaway programs in the scratch directory.

## Tasks

- [ ] **Memory link on the Mac:** `~/.claude/projects/-Users-user-src-re/memory` is an empty directory, not the link to `claude-memory/`; run
      `scripts/link-claude-memory.sh` and check a memory note reads through the link. If the script assumes GNU `ln`/`readlink` options, fix it for BSD
      userland (shared script: file the Linux check).
- [ ] **Side-phase setup:** `tests/phases.txt`'s phase list names "26 macOS port" and gets `flavors 26 plain,asan,tsan`; `CURRENT_PHASE` stays as it
      is. Add the side-phase rule to CLAUDE.md ("Tests run by phase": a side phase listed in `SIDE_PHASES` is added to `--affected` runs on the machine
      doing it; its days quote their own `ran X of Y` line) **marked as proposed until the user confirms it**; `docs/guide/building-and-testing.md`
      names it. The tooling change itself is Day 270.
- [ ] **Case-insensitive checkout:** list paths in the tree that differ only in case (`git ls-files | sort -f | uniq -di`), and grep the gates and tests
      for assumptions that the file system is case-sensitive (fixtures named `Foo`/`foo`, tests that create both). Record each; none may stay.
- [ ] **Prerequisites** (phase file list): install `slangc` v2026.18.2 for macOS and check it mints byte-identical SPIR-V for three shaders against the
      committed `*.spv.inc`; record every version (Command Line Tools, Apple clang, SDK version from `xcrun --show-sdk-version`, CMake, Ninja, Python, `lldb`,
      `dsymutil`). Write the **macOS section of `BUILD.md`** (required, for checks, optional; MacPorts install commands; kitty's `slangc` shadowing
      warning; the Day 266 decision on Xcode).
- [ ] **Inventory** (a table in the spec: file, what is Linux-specific, macOS counterpart, shared `posix/` or `macos/`, the port day): every file under
      `os/linux/` (with `debug/`), `io/linux/`, `settings/linux/`, `engine/platform/linux/`, every `#if ENGINE_PLATFORM_LINUX` in shared code
      (`io/path.h`, `debug/crash_event_platform.h`, `debug/crash_handler_sentry.cpp`, `platform/entry_point.h`, `tools/crash_extract/resolve.cpp`,
      `tools/docgen/src/samples.cpp`), every CMake `if(LINUX)`/`CMAKE_SYSTEM_NAME` branch, every script under `scripts/`, every test that is Linux-only, and
      each `KNOWN-ISSUES.md` line that names Phase 26.
- [ ] **Spike A — the build.** Remove the Darwin stop locally (not committed) and configure with Apple clang and Ninja; record every failure class with
      one example (CMake branches, missing platform sources, Apple header macros, warnings Apple clang raises that Linux clang does not, link flags such
      as `--build-id` and `-Wl,--gc-sections`). Time a full unity build of `foundation_base` on this 4-core machine.
- [ ] **Spike B — Objective-C++ under the rules.** A throwaway `.mm` that includes `<AppKit/AppKit.h>` and `<Metal/Metal.h>`, compiled with
      `-fobjc-arc -Wall -Wextra -Werror` in a unity group with a C++ file of ours: list the macros that leak (`check`, `verify`, `require`, `nil`, `Nil`, `YES`,
      `NO`, `BOOL`, MacTypes `Point`/`Rect`/`Handle`/`Size`) and confirm `apple_enable.h`/`apple_disable.h` with `push_macro` contain them; confirm the
      opaque-pointer pattern (a C++ class owning an `id<MTLDevice>` through ARC in a `.mm`, the neutral header unchanged) leaks nothing under ASan.
- [ ] **Spike C — the window on the main thread.** A throwaway program: `NSApplication` without a bundle, a window with a `CAMetalLayer` view, a polled
      event loop (`nextEventMatchingMask:untilDate:distantPast`) driven from our own loop, clearing to a color each frame through Metal; confirm it shows,
      takes keyboard focus when started from a terminal (`activateIgnoringOtherApps`), resizes and closes, and measure the frame time.
- [ ] **The spec** `docs/superpowers/specs/2026-11-NN-macos-port-design.md`: each phase-file decision confirmed or amended with the evidence; the
      inventory; the `posix/` split; the side-phase rule; the decisions the spikes changed are written back into the phase file.
- [ ] **Documentation:** the spec is linked from `docs/README.md`; the phase file's day list is current; `ctest -L docs` passes (on Linux until Day 270).
- [ ] No code review (no product code beyond the setup). Update `STATUS.md` (a "Phase 26 (side)" line), `KNOWN-ISSUES.md` (the Linux checks for the
      shared files touched); commit and push.

**Done:** memory is linked on the Mac; the phase map knows phase 26 and the proposed side-phase rule is written; the checkout is free of case-only name
clashes; `BUILD.md` has a macOS section with every version recorded; the spec with the inventory and the three spike results is committed and linked.
