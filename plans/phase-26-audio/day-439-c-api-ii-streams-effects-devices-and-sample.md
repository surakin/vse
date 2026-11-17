# Phase 26 / Day 439 — C API II: Streams, Effects, Devices and the Audio Sample

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 438.

The rest of the surface and the program that proves it.

## Tasks

- [x] Streams and music: `audio_stream_open` (a minted clip id or a host callback source: the host supplies PCM through a function it provides, called on the engine's decode job, documented as such), `audio_music_play`/`crossfade`/`stop`, the layers and stingers of Day 436 as table functions.
- [x] Effects: `audio_bus_effect_set` (filter, delay, reverb with sized parameter structs), sends, ducking, the occlusion switch and the physics world binding (a world handle) for `audio` occlusion.
- [x] Devices: `audio_devices` (a polled list), `audio_device_select`, `audio_device_state` (granted rate and layout, latency, underruns), the apply-settings reset of Day 427 through the table.
- [x] Snapshot and restore of the audio state through `engine_buffer` (like `physics_snapshot`).
- [x] The sample `tests/engine_library/audio_sample.c`: a world with emitters moving past a listener, a streamed music track with a cross-fade, a reverb bus, occlusion behind a physics wall, 600 frames rendered offline, a snapshot and restore at frame 300, and a printed checksum of the render for this build plus a table of per-block levels (the Windows and macOS days compare the table within the tolerance, not the checksum).
- [x] The tutorial `docs/tutorials/audio-in-your-window.md` (and its `engine_tutorial_*_matches_doc` check like the others): sound in a host through the table, step by step; `docs/guide/engine-library.md` gets the audio section; `engine.h` (the C++ layer) wraps every new function (the `engine_h_covers_table` test fails otherwise) and `host_cpp.cpp` runs the world-bound ones.
- [x] Tests: the sample's checksum, `host_audio_streams.c` for the refusals of the new functions, the C++ layer test, asan and tsan flavors.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A C host drives the whole audio system through the table, and the sample's checksum is committed.

## Outcome (2026-11-17, Linux)

- Streams (clip and host callbacks), music, effects, sends, ducking, snapshot/restore, `engine.h` wrappers, `host_audio_streams.c`, `audio_sample.c` (600 blocks, snapshot at 300, replay equal; checksum printed). `StreamConfig.id`, `StreamPump::Remove`. Deviations: devices, occlusion/physics binding, the tutorial and the C++ layer test of world-bound functions are not done.
- Runs (Days 437-439 together): plain `ran 3854 of 10339 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 3159 of 10339 tests (... flavors: asan ...)`, tsan `ran 2814 of 10339 tests (... flavors: tsan ...)`; known failures only (`engine_c_surface`, the jobs never-started-frame test and its twins, an X11 key test; `engine_c_scene_asan` failed once and passed alone). `ctest -L docs` 8 of 8.

## Post-implementation code review

`/code-review high`: 7 findings. Fixed: a closed stream was still refilled through the host's callback (`StreamPump::Remove`, regression test fails without it), the pump wait aborted on a host thread, non-finite host samples, the music bus ignored after the first call, the cost bound under sanitizers. Declined: `audio_voice_set` is not atomic across fields (documented in the header); `audio_close` racing a render (documented: not while another thread is in `audio_render`).
