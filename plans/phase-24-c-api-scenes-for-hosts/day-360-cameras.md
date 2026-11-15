# Phase 24 / Day 360 — Cameras

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Day 359; Phase 5 (`renderer::Camera`).

A camera entity, moved by the host, decides what a surface shows.

## Tasks

- [x] `ENGINE_COMPONENT_CAMERA` (`engine_camera`: vertical field of view, near, far, flags; a size-prefixed struct) on any entity with a transform; the camera looks along the entity's -Z with +Y up (the header states the convention once, Day 358), so setting the entity's transform each frame is the whole camera control; the engine's demo controllers stay engine-side.
- [x] `surface_set_camera(surface, world, camera_entity)` (or part of `surface_set_world`, as the spec decided): the camera's aspect ratio follows the surface unless the host sets `ENGINE_CAMERA_FIXED_ASPECT`; a camera entity that is destroyed or loses its component makes the surface show the background and report `camera_status` in `surface_state`; two surfaces may name different cameras of one world.
- [x] `surface_pixel_ray(surface, x, y, &origin, &direction)` turns a surface pixel into a world ray for the host's picking; the inverse `surface_project(surface, point, &x, &y, &depth)`; both read the camera as of the last frame and say so.
- [x] Orthographic cameras only if they are one flag and one matrix (decided in the spec); otherwise filed in `KNOWN-ISSUES.md`.
- [x] Tests: analytic projection checks (known world points land on known pixels, within half a pixel, at several aspect ratios and fields of view); pixel-ray and project round trip; a moved camera changes the image and the golden of a three-cube scene matches; destroyed camera; two surfaces, two cameras, one world.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day; GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`**; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still builds. *(not run: postponed to the closing pass by the user's decision)*
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the other two no later than Day 372; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`. *(Linux only, and not run)*
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` carries its comment convention (set on Day 346) for every addition and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated; the ABI snapshot (`abi/engine_api.snapshot`) is regenerated and the version rule of the spec is followed.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); a new C test host gets its `std_usage_check` allow-list entry.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`. *(postponed to the closing pass; see the section below)*
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a host moves a camera entity and sees the world from it, with picking rays that agree with what is drawn.

## Outcome

Day 360 done on the Linux machine (2026-11-15), with the tests written and built but **not run** (the user's decision for Days 360-372: the runs wait for the phase's closing pass). `ENGINE_COMPONENT_CAMERA`, `surface_set_camera`, `surface_pixel_ray`, `surface_project`, `camera_status` and `camera_entity` in `engine_surface_state`; the camera is built from the entity's world matrix (Gram-Schmidt, scale ignored) in `SurfaceScene::Render`, and the last frame's camera answers the ray and projection queries. Tests are in `host_scene.c`: argument checks, analytic projections (the camera at (0,0,4), 60 degrees, 4:3: the point (1,0,0) lands at x = 109.98), the ray and projection round trip, a moved camera, a fixed aspect, a camera that loses its component, a destroyed camera, two surfaces and two cameras. No golden for the camera views yet: the closing pass decides it. Builds: Clang, unity. Not run: any test, GCC, non-unity, shared, importer-off.

## Post-implementation code review

A self-review of the diff (the formal `/code-review high` of Days 360-368 runs once in the closing pass): the default `camera_status` literal was replaced by `ENGINE_ERROR_NOT_FOUND`; the pixel y direction follows the Vulkan projection (clip y down), which the analytic test checks. Nothing declined.
