# Phase 24 / Day 367 — Labels: Text in the World

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 360, 362, 363; Phase 14 (text).

Name tags: text drawn at a point in the world, facing the camera.

## Tasks

> Day 358 decision 6 (spec): labels are the text renderer's 2D path projected from the world point: constant pixel size, no depth test, a size in metres rounded to a set of pixel sizes; billboard quads and depth-tested plates are later items. Measured: +0.14 ms per name tag, +1.4 ms for a hundred (1280x720).

- [ ] `ENGINE_COMPONENT_LABEL` (`engine_label`: UTF-8 text up to a bound, color, size mode (constant on screen in pixels or constant in metres), offset, anchor, an optional background plate color, flags): drawn at the entity's world position plus the offset, facing the camera, through Phase 14's glyph atlas and text renderer (or the quad path Day 358 chose); depth-tested against the scene unless the flag says always on top.
- [ ] A label on a joint (Day 363's attachment) follows it; a label on a hidden entity is hidden; a label count bound is reported (`labels_refused`).
- [ ] Text: line breaks, grapheme clusters, fonts from the host-provided font resource (the engine ships no font; the host adds one with `resources_add`, or the engine's default if the spec has one); text that needs a missing glyph shows the font's `.notdef` and counts it; bounds on length against hostile input.
- [ ] Text above the atlas's largest size is Phase 28's outline meshes; this phase clamps (blurry, counted), as the 2D text path already does.
- [ ] Tests: a label at a known point lands on a known pixel (center within a pixel) at two aspect ratios; constant-pixel-size labels keep their size when the camera moves away, constant-metre labels shrink; a plate; occlusion by a mesh; a hundred labels in one frame; UTF-8 and combining marks; a golden image.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day; GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`**; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still builds.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the other two no later than Day 372; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` carries its comment convention (set on Day 346) for every addition and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated; the ABI snapshot (`abi/engine_api.snapshot`) is regenerated and the version rule of the spec is followed.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); a new C test host gets its `std_usage_check` allow-list entry.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a host puts name tags in its 3D scene, anchored to entities and joints, with sizes and occlusion as documented.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
