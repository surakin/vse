# Phase 24 / Day 359 — A World on a Surface

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 349, 352, 353, 358; Phases 5, 17.

A surface draws a world instead of clearing: meshes of entities, seen through a fixed default camera until Day 360.

## Tasks

- [ ] `surface_set_world(surface, world)` and the unbinding call (a `NULL` world returns the surface to its background color); a destroyed world or a world destroyed under a surface unbinds it with a status in `surface_state` (`world_status`), never a crash; `destroy` of a world with a surface bound answers `IN_USE` or unbinds, as the spec decided.
- [ ] The surface owns what it renders with (Day 358's decision): a `renderer::Renderer` on its device and swapchain, a `RenderScene`, a `scene::SceneLink`; every `engine_frame` runs the world's systems in the documented order (transform, render sync) and then renders each bound surface; viewport size and scale follow `surface_resized`.
- [ ] `ENGINE_COMPONENT_MESH_REFERENCE` resolves a mesh and a material resource id (Phase 22 resources, host-provided content of Day 350) to GPU objects resident on the surface's device (Day 358 spike 2); a reference that does not resolve draws nothing for that entity and is counted in `surface_state` (`entities_unresolved`), with the reason in `last_error`; nothing blocks the frame on a load.
- [ ] `engine_caps` bit `WORLD_ON_SURFACE`; a build without the renderer (or a backend that cannot) refuses with `UNSUPPORTED`.
- [ ] `engine_frame_stats` / `surface_state` counters: objects drawn, objects culled, draw calls, GPU time if available; the frame budget (Day 353) still holds: the render of a surface that would overrun is the last thing skipped.
- [ ] Tests (C host, X11 window): a host-provided cube blob and a material, an entity with a transform and a mesh reference, `surface_set_world`, one frame, `surface_read_pixels` compared with a golden image under the rule of Day 358; the world destroyed under the surface; an entity whose mesh id does not exist; resize.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day; GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`**; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still builds.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the other two no later than Day 372; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` carries its comment convention (set on Day 346) for every addition and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated; the ABI snapshot (`abi/engine_api.snapshot`) is regenerated and the version rule of the spec is followed.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); a new C test host gets its `std_usage_check` allow-list entry.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a C host shows a host-provided mesh of a world entity in its own window and the read-back matches a golden image.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
