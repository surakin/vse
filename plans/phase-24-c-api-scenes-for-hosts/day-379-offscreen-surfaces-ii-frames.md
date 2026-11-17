# Phase 24 / Day 379 — Off-Screen Surfaces II: The Per-Frame Picture

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Day 378.

The path a toolkit host uses: take the newest finished frame every frame, without waiting for the GPU.

## Tasks

- [ ] `surface_acquire_frame(surface, &engine_frame_image)` and `surface_release_frame`: the newest finished frame as RGBA8 (width, height, stride, frame number, colour encoding and alpha stated), from a ring of mapped buffers that the device copies into (spike 2 and 3: depth, the copy, fences); acquiring never waits for the GPU, and a host slower than the frames skips pictures.
- [ ] Ownership and thread rules in the header and the guide: how long the pointer is valid (until release or surface destroy), which thread may read it, what a resize does to frames already acquired (they stay valid until released), at most `engine_surface_info.frames_in_hand` held at once (a held frame never blocks rendering: when all slots are held the frame is dropped and counted).
- [ ] `surface_state` reports frames rendered, taken and dropped, and the latency in frames of the newest one.
- [ ] The existing `surface_read_pixels` ticket path stays (screenshots, tests); its 256-ticket limit is untouched.
- [ ] Cost recorded at 1280x720 and 1920x1080: the copy per frame, CPU time in `engine_frame`, memory of the ring.
- [ ] Tests: acquire every frame from a host thread for 600 frames and compare frames with a golden image; a host that acquires every third frame (drops counted, no blocking, `engine_frame` time unchanged); a resize mid-run; destroy with a frame held (the pointer stays valid until release, or the call fails by value: as the spec says); asan and tsan.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the Windows PC and the Mac on Days 381-382, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` and the public headers carry `///` comments and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/engine-library.md` (and the renderer or scene guide for a public header there) updated; the ABI snapshot regenerated when the table or a struct changed; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); only wrapped types, no `std`, no raw platform macros; every GPU-owning object keeps its CPU source and registers for the device reset.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A host takes the newest finished picture of an off-screen surface every frame without ever blocking on the GPU.
