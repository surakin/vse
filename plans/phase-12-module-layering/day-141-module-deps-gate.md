# Phase 12 / Day 141 — The `module_deps_check` Gate

**Phase:** [Phase 12 — Module Layering](../phase-12-module-layering.md)
**Depends on:** Day 140 (the gate design in the spec).

Build the gate before changing any dependency, with today's violations allow-listed, so every later day is measured by the entries it deletes and nothing
regresses in between.

## Tasks

- [x] **`tools/module_deps_check`** (same form as the other source gates: a CMake script or a small C++ tool, the spec's choice): reads every `#include "..."` in
      `engine/`, `game/`, `tools/` (fixtures excluded), resolves it against the include directories, maps each file to a module through a map file
      (`tools/module_deps_check/modules.txt`: path glob → module and layer, plus the extra edges a module's row in `ARCHITECTURE.md` allows), and fails on
      (a) an edge to a higher layer, (b) a cycle between modules, (c) a program or neutral file including a per-platform directory (`os/<platform>/`,
      `io/<platform>/`, `rhi/<backend>/` outside its own backend), (d) an include it cannot resolve that is not a system header.
- [x] **Link edges too:** the same check over the CMake link lines (`target_link_libraries`), so a library cannot link a higher one even without an include
      (today: the renderer linking `rhi_vulkan`, the foundation exposing the display libraries).
- [x] **Allow list** (`tools/module_deps_check/allowlist.txt`, one entry per edge with a reason and the day that removes it): today's thirteen items of
      `ARCHITECTURE.md` section 2; a stale entry (an edge that no longer exists) fails, so each later day must delete what it fixed.
- [x] **Self-tests** (planted fixtures under `tools/module_deps_check/fixtures/`): an upward include, a two-module cycle, a program including `os/linux/`, an
      upward link line, a stale allow-list entry, an entry without a reason; each fails with a message naming the file and line; a clean fixture passes.
- [x] **Wiring:** ctest `module_deps_check` and its self-tests (phase 12 labels in `tests/phases.txt`, `engine_label_tests`, the `flavors 12 plain` line);
      `ARCHITECTURE.md` section 6 points at the gate instead of "re-measured by hand"; the gate's output format (module, edge, file:line) documented there.
- [x] **`CLAUDE.md`:** the module-dependency rule names the gate and its allow list.
- [x] **Runs (Linux):** `scripts/test-phase.sh --affected` (a new tool and the phase map: everything) and the gate on a clean checkout; quote the lines.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: a gate that passes because it resolved nothing (the self-test counts resolved edges on the
      real tree and fails below a floor), include resolution order matching the compiler's, generated files and `.inc` tables.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md` (the thirteen allow-listed edges as one entry pointing at the allow list); commit and push.

**Done:** `module_deps_check` passes on the tree with exactly today's thirteen violations allow-listed (each with its removal day), fails on every planted kind
of violation, and is part of ctest.

## Result (2026-11-07)

- `tools/module_deps_check/` (CMake script like the other gates): `modules.txt` (module map, layers, `allow`/`forbid`/`platform-dir`/`target`/`syslib`
  lines), `allowlist.txt` (17 entries, each with reason and removal day: the 13 items of `ARCHITECTURE.md` section 2 broken into edges), `check.cmake`,
  fixtures for each planted kind. The real tree finds 142 module edges (floor 100) and passes with the allow list.
- Link lines: only literal `target_link_libraries` items are read (a `${foundation_lib}` item is skipped), so the include check is the main gate and the
  link check covers the lines written by name (it found `renderer -> rhi_vulkan` and the display libraries). `tests/` CMake files are scanned from Day 145.
- Runs: `scripts/test-phase.sh --affected` = `ran 1354 of 3477 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, one failure,
  the known `renderer_demo_script_wayland`; `ctest -L docs` passes; 14 `module_deps_check*` tests pass. A planted `debug/` include in `mem/` makes the gate fail.

## Post-implementation code review

`/code-review high`, 4 findings. Fixed: a target or syslib owner whose module has no layer passed every rule silently (now an error, with a regression
fixture `module_deps_check_no_layer`). Declined: the allow list is per module pair, so a new include inside an already allow-listed pair is covered until that
entry is deleted (by design, each entry names its removal day); the line-number lookup treats the include text as a regex (cosmetic); `tests/` not scanned
(scheduled, Day 145).
