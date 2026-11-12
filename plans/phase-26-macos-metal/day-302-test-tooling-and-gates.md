# Phase 26 / Day 302 — Test Tooling, Gates and Docs on macOS

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 301.

From today every macOS day is scoped and gated like a Linux day: `scripts/test-phase.sh` runs on the Mac, knows the side phase, and every gate and the
docs build pass here.

## Tasks

- [x] **`scripts/test-phase.sh` on macOS:** default build directory `build-macos` on Darwin; run it under `/bin/bash` 3.2 and fix what fails (associative
      arrays, `mapfile`, `${x,,}`, GNU `sed -i`, `readlink -f`, `date +%N`, `nproc`); a bash newer than 3.2 is required only if a feature has no 3.2 form
      (then `BUILD.md` says so). The flavor list on macOS is `plain,asan,tsan`; `--flavor all` runs the three.
- [x] **Side phases:** a `SIDE_PHASES` file (one phase number per line, `26` today) read by the script; `--affected` adds every side phase to the selection
      on any machine that builds its tests, and the summary line names it (`phases: 18, 26 (side), ...`). `test_phase_labels` accepts labels of a side
      phase above `CURRENT_PHASE` and fails on a label above it that is not a side phase. Tests for the script (`tests/test_phase_script`) cover both.
- [x] **Gates on macOS:** build and run `module_deps_check`, `platform_neutral_check`, `std_usage_check`, `file_access_check`, `config_access_check`,
      `signal_safety_check`, `raw_new_check`, `rhi_boundary_check`, `settings_fixtures_check`, `ui_widget_style_check`, `test_phase_labels`; fix every
      path, `find`/`sed` or case assumption that breaks on macOS.
- [x] **The rules learn macOS** (`platform_neutral_check`): AppKit, Foundation, QuartzCore, CoreGraphics, IOKit, Metal, Mach, `dispatch`, `copyfile`,
      `sysctl` and `objc` headers only under `*/macos/` (and `engine/rhi/metal/` from Day 316), `.mm` files only there too; planted-violation fixtures for
      a framework header in a neutral file and a `.mm` outside a macOS directory. `std_usage_check` scans `.mm` files and treats framework headers as OS
      headers (not standard); a planted `<vector>` in a `.mm` still fails.
- [x] **Cross-syntax gates:** the `*_cross_syntax_macos` checks (types, io, settings, debug, rhi) keep their meaning now that a real macOS build exists:
      keep them (they cover arm64 syntax, which this Mac does not build natively) and say so in their comments.
- [x] **Moved (amended on Day 302): to Day 309 (docgen, the reference, `docs_check`/`links`/`lint`) and Day 327 (`docs_samples`).** docgen
      needs `io/` and `debug/failure_hooks.h`, which the Mac builds from Days 303-304 and 306-308, and its samples link the renderer, UI and text, which
      arrive with Days 310-325; until then the reference and `docgen lint` run on the Linux machine (`KNOWN-ISSUES.md`). Original task: build the tool, `cmake --build build-macos --target docs` produces the same `docs/reference/` bytes as Linux (compare, fix any
      ordering or path difference), and `ctest -L docs` passes, including the guide samples (`tools/docgen/src/samples.cpp`'s platform branch).
- [x] **Docs:** `docs/guide/building-and-testing.md` ("Test phases and scoped runs": side phases; "Building and testing on macOS": the script, the flavors,
      no LeakSanitizer).
- [x] **Runs:** `scripts/test-phase.sh --affected` on macOS in all three flavors (quote the lines); the Linux and Windows runs of the shared script and gate
      changes filed.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the script's portability fixes changing nothing on Linux and Git Bash, the label check
      for side phases (a typo cannot hide a test).
- [x] Update `STATUS.md` (the macOS commands), `KNOWN-ISSUES.md`; commit and push.

**Done:** `scripts/test-phase.sh --affected --flavor all` runs on the Mac and quotes `ran X of Y tests (phases ...)` with phase 26 named as a side phase,
and every gate passes on macOS with the new macOS rules and their planted fixtures. (The reference and `ctest -L docs` moved to Days 309 and 326.)

## Result (2026-11-11, the Mac)

- **The script** (`scripts/test-phase.sh`) needed less than the plan feared: it already used no bash-4 feature. It now knows macOS (`uname`
  `Darwin`: default `build-macos`, the configure hint names the `macos-clang` preset), counts CPUs with `sysctl -n hw.ncpu` where `nproc` is
  missing, and documents the bash 3.2 limit. Run with `/bin/bash` 3.2.57 throughout.
- **Side phases.** `SIDE_PHASES` (26) is read by the script (`--side-phases-file` for tests): `--affected` adds each side phase the build has tests
  of (counted with `ctest -N -L ^phaseN$`), and the summary marks it (`phases: 19 26 (side)`); `--current` and a phase number never add one.
  `test_phase_labels` reads `CURRENT_PHASE` and `SIDE_PHASES` and fails on any phase of the map above the current one that is not a side phase,
  on every machine (map-based, so a mistyped map line is caught where its tests are not built). New cases: `test_phase_script_affected_side_phase`,
  `_side_phase_not_built`, `_side_phase_already_selected`, `_current_ignores_side_phases`, `_bad_side_phases`; `test_phase_labels_planted_above_current`
  and `_planted_no_side_phase`. Every older script case reads an empty side-phase fixture, so it means the same on the Mac as on Linux. macOS cases
  for the sanitizer selections (phases 16, 10, 26) and the flavor partition (0, 16, 26), as Windows has.
- **Gates on macOS:** all twelve source gates (`rhi_boundary`, `raw_new`, `file_access`, `std_usage`, `settings_fixtures`, `config_access`,
  `signal_safety`, `platform_neutral`, `module_deps`, `ui_widget_style`, `sqlite_boundary`, `database_sql`) with their self-tests, `test_phase_labels`
  and `tests/test_phase_script` configure and pass; no path, `find`, `sed` or case problem turned up (they are CMake scripts). The map's macOS flavor
  lines gained the gates' phases (4, 5, 7, 9, 11, 19).
- **The rules learn macOS.** `platform_neutral_check` rule 7: Apple's headers (the frameworks, `<mach/...>`, `<mach-o/...>`, `<dispatch/...>`,
  `<objc/...>`, `<copyfile.h>`, `<sys/sysctl.h>`, `<libproc.h>`, `<crt_externs.h>`), by `#include` or `#import`, and `.mm` files only under a
  `macos/` folder or `engine/rhi/metal/`; planted: a `.mm` and three framework includes outside (29 violations now), a clean `.mm` and a
  `tests/macos` file. **Found while doing it: `std_usage_check` and `module_deps_check` did not read `#import`**, which Objective-C++ accepts for
  any header, so a `.mm` could reach `<vector>` or an upper layer unseen; both now match `#include` and `#import`. Planted: `<vector>` by
  `#include` and `<string>` by `#import` in a `.mm` (`std_usage_check_planted_lines`), and an upward `#import` in a `.mm`
  (`module_deps_check_upward_objc`).
- **Cross-syntax checks** kept, with a comment in each of the five `CMakeLists.txt`: they are what compiles those headers for arm64 until Day 327.
- **docgen moved (plan amended):** it needs the file layer and `debug/failure_hooks.h` (Days 303-308 on the Mac) and its samples link the renderer and
  UI; the reference and `docs_check`/`links`/`lint` go to Day 309, `docs_samples` to Day 327.
- **`tests/module_map.txt`** maps what was missing: `tests/macos` (26), `os/macos` and `os/posix`, `SIDE_PHASES` and `CURRENT_PHASE` (6: the label
  check reads them), `ARCHITECTURE.md`, `BUILD.md`, and the main line's `tests/database`, `sqlite_boundary_check` and `database_sql_check` (19),
  which sent every `--affected` run to the full suite.
- **Runs (macOS, `/bin/bash scripts/test-phase.sh`):** `--affected` (everything, since CMake files changed): plain `ran 344 of 498 tests (phases: all;
  flavors: plain; ...)`, asan `ran 77 of 498`, tsan `ran 77 of 498`, all passed; scoped to a plan change: `ran 29 of 498 tests (phases: 19 26 (side);
  flavors: plain; ...)`, asan 11 of 498, tsan 11 of 498; `--all --flavor all`: `ran 498 of 498 tests (phases: all; flavors: all; display: included;
  crash tests: included)` in unity and in non-unity (`build-macos-nounity`). No warnings. (After the review's fixes; before them the same runs
  passed with 495 tests.)

## Post-implementation code review

`/code-review high` on the day's diff: 10 findings, **all 10 fixed**.

1. **The side phases were counted before the build**, while Catch2 tests are registered only when their executable is built: a fresh tree, or a side
   phase's first test file, could drop the phase from `--affected`. The count now runs after `cmake --build`. (A dry run does not build, so no
   script case can show the difference; the order is the fix.)
2. **The side-phase count ignored the flavor and the left-out labels**, so `--flavor asan` could name a side phase none of whose tests ran. It now
   counts with the run's own `-L`/`-LE`. New case `test_phase_script_affected_side_phase_wrong_flavor` (phase 6, plain tests only, under
   `--flavor tsan`): failed with the count's filters removed, passes with them.
3. **`#import` was matched in two gates only.** `rhi_boundary_check` (three expressions), `sqlite_boundary_check` and `platform_neutral_check` rules 1,
   2 and 4 now match `#include` and `#import` (the capture indices adjusted). Planted: `#import <vulkan/vulkan.h>` in a `.mm`
   (`rhi_boundary_check_planted_count`, 12 problems) and `#import <sentry.h>` in a `.mm` (`platform_neutral_check_planted_count`).
4. **`signal_safety_check` and `database_sql_check` did not scan `.mm`** (`database_sql_check` not `.cc` either): both do now, before the macOS
   crash backend (Days 306-308) adds Objective-C++ `SIGNAL-SAFE` code.
5. **Rule 7's list missed Darwin headers**: it now also covers `<os/...>`, `<libkern/...>`, `<malloc/...>`, `<simd/...>`, `<sys/event.h>`,
   `<TargetConditionals.h>`, `<Availability*.h>`, CoreText, CoreImage, ImageIO, Accelerate and MetalPerformanceShaders (planted:
   `<TargetConditionals.h>`, `#import <os/log.h>`; a quoted `"os/os.h"` is not flagged).
6. **A stale or mistyped `SIDE_PHASES` entry passed.** An entry at or below `CURRENT_PHASE`, or with no map lines, now fails
   (`test_phase_labels_planted_stale_side_phase`: `3` and `62`).
7. **An empty or missing `CURRENT_PHASE` crashed the check** in `list(GET)`; it now reports the file (`test_phase_labels_planted_empty_current`).
8. **The side-phase block's own "configure first" message** hid the per-platform one; it is gone, and the per-platform check runs first.
9. **Rule 7 read every file line by line again, and missed `.m`**: it now reads only the files that have an angle-bracket include
   (`file(STRINGS ... REGEX)`), and `.m` files outside the macOS directories fail like `.mm` (planted `stray_c.m`; 33 violations in all).
10. **Rule 7 sat between rules 5 and 6**: moved after rule 6, in the header and in the code.
