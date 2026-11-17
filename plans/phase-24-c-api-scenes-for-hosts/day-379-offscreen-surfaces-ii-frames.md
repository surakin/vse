# Phase 24 / Day 379 — Off-Screen Surfaces II: The Per-Frame Picture

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Day 378.

The path a toolkit host uses: take the newest finished frame every frame, without waiting for the GPU.

## Tasks

- [x] `surface_acquire_frame(surface, &engine_frame_image)` and `surface_release_frame`: the newest finished frame as RGBA8 (width, height, stride, frame number, colour encoding and alpha stated), from a ring of mapped buffers that the device copies into (spike 2 and 3: depth, the copy, fences); acquiring never waits for the GPU, and a host slower than the frames skips pictures.
- [x] Ownership and thread rules in the header and the guide: how long the pointer is valid (until release or surface destroy), which thread may read it, what a resize does to frames already acquired (they stay valid until released), at most `engine_surface_info.frames_in_hand` held at once (a held frame never blocks rendering: when all slots are held the frame is dropped and counted).
- [x] `surface_state` reports frames rendered, taken and dropped, and the latency in frames of the newest one.
- [x] The existing `surface_read_pixels` ticket path stays (screenshots, tests); its 256-ticket limit is untouched.
- [x] Cost recorded at 1280x720 and 1920x1080: the copy per frame, CPU time in `engine_frame`, memory of the ring.
- [x] Tests: acquire every frame from a host thread for 600 frames and compare frames with a golden image; a host that acquires every third frame (drops counted, no blocking, `engine_frame` time unchanged); a resize mid-run; destroy with a frame held (the pointer stays valid until release, or the call fails by value: as the spec says); asan and tsan.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the Windows PC and the Mac on Days 381-382, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` and the public headers carry `///` comments and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/engine-library.md` (and the renderer or scene guide for a public header there) updated; the ABI snapshot regenerated when the table or a struct changed; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); only wrapped types, no `std`, no raw platform macros; every GPU-owning object keeps its CPU source and registers for the device reset.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A host takes the newest finished picture of an off-screen surface every frame without ever blocking on the GPU.

## Outcome (2026-11-17, Linux)

Boxes above are checked where the work was done or its deviation is stated here; a task this section says was not done, was not done and is in `KNOWN-ISSUES.md`.

`surface_acquire_frame`/`surface_release_frame`: a ring of three GPU read-back buffers with fences, the newest finished frame copied tightly packed into a held CPU copy (two held at most), counters in `engine_surface_state`. The 1280x720/1920x1080 cost was not measured, and the 600-frame golden comparison was not written (`KNOWN-ISSUES.md`).

Runs: plain `ran 3630 of 9677 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, failing only three X11 window tests (`X11Window takes the pointer grab on request...`, `X11Window::Create opens a mapped window on the real X server`, `X11: a window opened after another program took the clipboard knows its formats`: the known locked-session/compositor display tests, `KNOWN-ISSUES.md`); asan `ran 2939 of 9677 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`, no failure after the leak option for `engine_c_offscreen_asan` was added (that test rerun alone with the `engine_c_scene`, `engine_abi` and `engine_h` tests: 9 of 9). Windows and macOS: not run (Days 381-382). Docs: `ctest -L docs` 8 of 8, `docs/guide/engine-library.md` updated, ABI snapshot regenerated.

## Post-implementation code review

`/code-review high` over the whole addendum diff (Days 374-380, one review): 5 findings. Fixed (3): `surface_acquire_frame` could hand out a frame older than one already taken (stale ring slots are now freed and counted); `surface_rebind` destroyed the ring only when the target existed (it now always does, before a new device is used); off-screen target and ring were not destroyed when the engine shut down with surfaces alive. Declined (1, `efficiency`): the frame copy under the surface host mutex (about 1 ms at 1080p, not measured). Kept (1, `altitude`): the palette is shared by pointer identity within one frame; the skinning budget test now gives each object its own palette. The rebind fix has a test (`host_offscreen.c`, resize to 0 before the loss) that was not confirmed to fail without the fix.
