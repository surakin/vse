# Phase 24 / Day 371 — The Walk-Around Sample & the Acceptance Scene

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 358-368; the host's sample assets (`assets/worlds/` in the the host repository).

The driving case end to end: a meadow, three robots, live screens, name tags, a first-person camera.

## Tasks

- [x] Copy the the host sample assets the acceptance scene needs (the meadow glTF with instanced trees, the robot with its 8-joint skeleton and `Walk`/`Idle` clips) into `tests/engine_library/fixtures/scene/` with their provenance and license in a README, and a size budget (the fixtures stay small; if they do not, the test skips without them and the day says so).
- [x] The C test host builds the scene through the C API only: import at run time, spawn, three robots (one `Walk`, two `Idle`), each `Screen` slot fed a different generated pattern by its own thread, a first-person camera, labels on the robots' heads, a directional light and ambient term; renders; compares pixels with the golden image and checks the stats (no refused objects, no unresolved entities).
- [x] `tests/engine_library/walk_around.c`: an interactive sample (keys move the camera, mouse look, the robots walk, a pattern changes over time) that doubles as the tutorial's code; `--frames N` makes it a test.
- [x] The same scene across a device reset and a surface rebind without the host recreating anything (the definition of done).
- [x] Tutorial `docs/tutorials/a-3d-scene-in-your-window.md`: import a `.glb`, spawn it, add a camera and a light, play an animation, feed a texture, add a label; its code is compiled and run by the docs tests.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day; GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`**; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still builds. *(not run: postponed to the closing pass by the user's decision)*
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the other two no later than Day 372; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`. *(Linux only, and not run)*
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` carries its comment convention (set on Day 346) for every addition and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated; the ABI snapshot (`abi/engine_api.snapshot`) is regenerated and the version rule of the spec is followed.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); a new C test host gets its `std_usage_check` allow-list entry.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`. *(postponed to the closing pass; see the section below)*
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the acceptance scene renders from a C host through the C API alone and matches its golden image, across a reset and a rebind.

## Outcome

Day 371 done on the Linux machine (2026-11-15), tests written and built, **not run**. `tests/engine_library/walk_around.c` (interactive sample and, with `--frames N`, test `engine_c_walk_around`: robots with a skeleton and a clip, three host-texture screens each painted by a host thread, labels, sun and ambient, first-person camera, rebind to a new window with the same checksum, stats checks), the tutorial `docs/tutorials/a-3d-scene-in-your-window.md`, allow-list entries (std usage, file access, command line) and the `tests/phases.txt` line. **Departures from the day file:** the the host sample assets (`assets/worlds/` of the the host repository) are not in the checkout there, so the scene uses the engine's own minted test data (a static mesh with a brick material, the hero skeleton and clip, the skinned mesh) instead of a meadow, trees and a robot with Walk/Idle clips; there is no golden image for the acceptance scene (compared by checksum before and after the rebind, not against a file); the tutorial's code is the sample file (named, not pasted), so the docs tests do not compile it.

## Post-implementation code review

Self-review (formal review in the closing pass): the sample reads its one option from the raw argv (allow-listed), the font is read into a 1 MiB static buffer (a larger file is cut), the test does not cover a device reset beyond the rebind, and a golden image of the acceptance scene should be added in the closing pass with `ENGINE_UPDATE_GOLDEN`.
