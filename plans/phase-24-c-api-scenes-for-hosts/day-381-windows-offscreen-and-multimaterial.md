# Phase 24 / Day 381 — Windows: Off-Screen Surfaces and Multi-Material Meshes (Vulkan and Direct3D 12)

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 373-380 built on Linux; Phases 13, 23 Day 354. **Done on the Windows PC.**

Both features on both Windows backends.

## Tasks

- [x] Build and run every test of Days 374-380 on `windows-msvc` (plain and asan) and `windows-clang-cl` (plain), shared and static; fix what differs.
- [x] Direct3D 12: the off-screen target, the copy into the readback ring (a readback heap, fences per slot, row pitch alignment 256 bytes converted to the stride the API promises), the device-removed path; Vulkan: the same through the Linux code.
- [x] Golden images of the multi-material scene and the off-screen acceptance scene on both backends under the tolerance of Day 358; differences between the backends are measured and recorded, not hidden by a larger tolerance.
- [x] The Win32 C host's walk-around sample draws its off-screen mode (Day 383 compiles here).
- [x] Record in `KNOWN-ISSUES.md` what could not be done and anything that moved from Linux.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The Day 374-380 tests pass on the Windows PC on both backends, or each failure is recorded with its observed behavior.

## Outcome

Skipped on the Linux machine by the user (2026-11-17); done on the Windows PC on 2026-11-17 (AMD Radeon 860M), at `8403904b` with the changes below.

- **Builds.** `build-msvc`, `build-clang-cl`, `build-windows-msvc-nounity`, `build-windows-clang-cl-nounity`, `build-msvc-shared` and
  `build-clang-cl-shared` build clean (warnings are errors) after fixes of code that cl and clang-cl refused: `engine/scene/level_spawner.cpp` (a
  slot's `id` hid the function's parameter, C4457: now `slot_id`), and in Phase 26's audio tests a local `kBlocks` that hid another file's in the unity
  build (now `kControlBlocks`), a `size_t` passed as `u32`, an `f64` NaN narrowed to `f32`, two constant divisions by zero (now
  `Limits<f32>::Infinity()` and `QuietNaN()`) and, under clang-cl, `backend_test.cpp`'s `CountingRender`, unused outside Linux (moved into the
  Linux block).
- **Both backends.** The off-screen hosts took the platform's own API only; `tests/engine_library/host_surface_api.h` now holds
  `host_surface_api_flags` (Day 369's `HOST_SURFACE_API`, moved out of `host_window.h`), the off-screen hosts read it, and Windows registers
  `engine_c_offscreen_vulkan`, `engine_c_acceptance_scene_vulkan` and `engine_c_walk_around_offscreen_d3d12` / `_vulkan` beside the Direct3D 12 tests
  (plain and asan). All pass in the four static trees, plain and asan, three times each.
- **Found: the loss test assumed Vulkan.** `engine_c_offscreen` failed on Direct3D 12 at its lost device: the second surface shares the adapter's one
  `ID3D12Device`, so the first surface's rebind answers `ENGINE_ERROR_UNSUPPORTED` while the second still holds the removed device (Day 369's rule).
  The test now does what the guide tells a host: rebind the second, then the first again; on Vulkan the first rebind still has to succeed.
- **The Win32 host shows its off-screen pictures.** `host_window_show` was a stub returning 0; it now converts the RGBA rows to a top-down 32-bit DIB
  and draws it with `SetDIBitsToDevice` (`gdi32`), and `host_window_can_show` is 1, so `walk_around --offscreen` checks that every picture taken was
  shown.
- **Pictures.** The golden scene: Direct3D 12 and Vulkan byte for byte the same, both within 2 of the Linux golden (40 of 76 800 values off by more than
  1), as on Day 369. The host acceptance picture (1280x720, `HOST_DUMP`): the two backends differ in 2 of 2 764 800 values, by 1. The multi-material
  checks of `host_scene.c` (slot colours by index and by name, an unmounted slot) pass on both. No tolerance was changed.
- **Not done:** the `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` build (no such tree on the PC); the review's regression test for the stride bound (a
  caller cannot reach it from the tests).

Runs. The full suite in place of `--affected` (it also closes Phase 25 on the PC, `plans/phase-25-physics/closing-runs-pc-mac.md`), with the
phase 8 reset-loop test left out by the user's choice (it froze the PC on Day 410): `build-msvc` `ran 6902 of 6904 tests (every test but `Resets in a loop on a real device ...` and its asan twin; flavors: plain and asan; display and crash tests included) in 625 s`, 10 failures: the six audio hash
checks (Linux's values, below), `docs_check` (`docs/reference/symbols.txt` out of date: `engine/audio/audio_system.h` had gained a line since it was
generated; regenerated), `async: a load by path waits for the table, and fails with NotFound once every table is in` (phase 22), `Skewed load: other
participants execute stolen work [asan]` and `Throughput scales with participant count` (each passing 3 of 3 alone afterwards); `build-clang-cl`
`ran 3760 of 3761 tests (every test but `Resets in a loop on a real device ...`; flavor: plain; display and crash tests included) in 1346 s`, 3 failures, the audio hash checks. After the fixes, in each of the four static trees: the off-screen, the host, walk-around and hash
tests and the async test, 3 of 3 runs passed; `ctest -L "^phase26$"` 310 of 310 (`build-windows-msvc-nounity`) and 156 of 156
(`build-windows-clang-cl-nounity`); `engine_library` 120 of 120 (`build-msvc-shared`) and 97 of 97 (`build-clang-cl-shared`); `ctest -L docs` 7 of
7. **Audio hashes:** the scene hashes of `mixer_test.cpp`, `effect_test.cpp` and `doppler_test.cpp` are a same-build regression check
(`docs/guide/audio.md`); cl and clang-cl give the same six values here, which differ from Linux's, so `tests/audio/platform_hash.h` keeps a value per
platform (Windows' committed here; the Mac still uses Linux's, `KNOWN-ISSUES.md`).

**After the rebase onto Days 431-436** (`b83265b1`): two more MSVC build fixes (`engine/audio/vorbis.cpp`'s `BuildIndex` locals `sync` and `pages`, which hid members, now `index_sync` and `index_pages`; `tests/audio/stream_test.cpp`'s inner `out`, now `wide_file`) and Windows' own Vorbis decode hashes (`vorbis_decode_test.cpp`, cl and clang-cl agree). Phase 26 in the four static trees: 408 of 410 (`build-msvc`) and 205 of 206 (`build-clang-cl`) before the Vorbis values, then the Vorbis and stream tests 137 of 137 and 69 of 69 in each; `engine_library` 120 of 120 in `build-msvc-shared` and 97 of 97 in `build-clang-cl-shared`.

## Post-implementation code review

`/code-review high` over the day's diff, done inline: 3 findings. **Fixed (1):** `host_window_show` compared the stride with `width * 4u`, which wraps
for a width of 2^30 or more: widths and heights over 32 768 are refused. **Recorded (1):** `PlatformHash` gives the Mac Linux's audio values, which it
is not expected to reproduce (`KNOWN-ISSUES.md`). **No change (1):** the loss test's retry after `ENGINE_ERROR_UNSUPPORTED` cannot hide a wrong refusal:
it requires the second surface to be lost, and on Vulkan the branch is not taken.

