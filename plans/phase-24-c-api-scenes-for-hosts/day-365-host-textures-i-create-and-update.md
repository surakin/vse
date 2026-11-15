# Phase 24 / Day 365 — Host Textures I: Create, Update, Resize

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 350, 358 (spike 3); Phase 8.

A texture whose pixels the host replaces every frame (live video), updated from any host thread.

## Tasks

- [x] `texture_create(engine, &engine_texture_info, &texture)` (RGBA8 with an sRGB flag, width, height, a name), `texture_update(texture, pixels, stride, &engine_update_info)` from any host thread (the engine copies and uploads on a later frame; a newer update replaces a pending one, so a slow frame never queues video), `texture_resize(texture, width, height)` for a source that changes resolution, `texture_destroy`; the texture counts as an object of the engine until destroyed.
- [x] The pending slot is lock-free or a short mutex around a pointer swap (decided by the spike, and tested under tsan): the update call never blocks on the GPU and never allocates more than the two frame buffers it keeps; a stride below the row size or above a limit is refused; sizes are bounded (`assets.host_texture.max_size`, a setting).
- [x] Uploads are budgeted per frame (`engine_frame_info.upload_budget_bytes`, default from the spec) and counted in the stats (`textures_uploaded`, `bytes_uploaded`, `updates_replaced`); a texture updated faster than frames are rendered costs one upload per frame.
- [x] The engine keeps the last pixels on the CPU so the texture survives a device reset and shows on a second device (Day 368).
- [x] `engine_caps` bit `HOST_TEXTURES`; a texture used before its first update shows a documented fill (magenta in a debug setting, else the base color).
- [x] Tests (headless, null device, then GPU): create, update, read back through a debug read of the texture (`texture_read_pixels`, test extension or public, as the spec decides) after a frame; replaced updates counted; resize; bad arguments; 24 destroys while an update is pending; the memory held is bounded across a long run.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day; GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`**; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still builds. *(not run: postponed to the closing pass by the user's decision)*
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the other two no later than Day 372; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`. *(Linux only, and not run)*
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` carries its comment convention (set on Day 346) for every addition and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated; the ABI snapshot (`abi/engine_api.snapshot`) is regenerated and the version rule of the spec is followed.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); a new C test host gets its `std_usage_check` allow-list entry.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`. *(postponed to the closing pass; see the section below)*
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a host creates a texture and updates it every frame from another thread, with only the newest frame uploaded and the limits enforced.

## Outcome

Day 365 done on the Linux machine (2026-11-15), tests written and built, **not run**. New `engine/engine_c/host_texture.{h,cpp}`: `HostTexture` (latest-wins pixels under a mutex, version counter, `CopyIfNewer` for a device's upload, resize drops pixels), `TextureHost` (slot table, `HostTexturePtr` shared unit, 4096 textures), settings `assets.host_texture.max_size`/`upload_budget`, the five table functions (`texture_create/update/resize/destroy/read_pixels`), `ENGINE_CAP_HOST_TEXTURES`, frame-stat fields (`textures_uploaded`, `bytes_uploaded`, `updates_replaced`, `slots_reverted`; the counters are read and reset by `frame`). Also commits the two security fixes of Day 364 (level import and spawn: sorted id tables and binary search instead of quadratic scans). Test: `host_import.c` `TestTextures` (argument errors, padded stride, resize through update, producer thread against frames, stale handles, 24 create/update/destroy rounds).

## Post-implementation code review

Self-review (formal `/code-review high` in the closing pass): the update copies under the texture's own lock, so a very large update blocks a device's copy for its duration (bounded by 8192x8192x4); a destroyed texture a surface still holds lives until that surface's next frame.
