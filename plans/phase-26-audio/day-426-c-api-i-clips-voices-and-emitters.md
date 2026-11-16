# Phase 26 / Day 426 — C API I: Clips, Voices, Buses and Emitters

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 425; Phases 23-24 (the C API).

A host loads sounds and plays them: through the table, ids and sized structs, no engine pointer.

## Tasks

- [ ] Table additions at the end of `engine_api` (the struct-size rule of the header honoured for every grown struct): `audio_clip_create` (from PCM bytes the host provides, or from a resource id of a minted clip), `audio_clip_destroy`, `audio_play` (clip, bus, gain, pitch, pan, loop, a `engine_audio_voice` handle out), `audio_voice_set` (a sized struct of parameters), `audio_voice_stop`, `audio_bus_set` (gain, mute), `audio_set_listener`, and the master controls; handles are generation-checked.
- [ ] Components: `ENGINE_COMPONENT_AUDIO_SOURCE` and `ENGINE_COMPONENT_AUDIO_LISTENER` on entities (the frame runs the sync of Day 416), with the same fields as the engine components; the component get returns what was set.
- [ ] Host-driven mode: an engine created with `audio.device = host` does not open a device; the host calls `audio_render(frames, out_f32)` from its own audio callback (the engine renders the mix into the host's buffer, no engine thread involved, lock-free and allocation-free in that call), so a host with its own device (a game engine plugin, a DAW-like tool, the host) keeps the output; the offline mode is the same call.
- [ ] Thread rules (the header's text extended): every function callable from any thread except `audio_render`, which is for the host's audio thread only; `audio_render` never blocks and never calls a host callback.
- [ ] Events as polled arrays (`audio_events`: voice finished, looped, virtualised, stream underrun) like physics, no callbacks across the boundary.
- [ ] The ABI snapshot (`abi/engine_api.snapshot`) regenerated and checked; `tests/engine_library/host_audio.c` exercises every function and refuses every bad argument (stale ids, struct sizes, NULL, huge counts).
- [ ] Tests: play a host-supplied PCM clip through `audio_render` and compare to the engine-internal render hash; a minted clip plays through the resource manager; a stale voice id is a status; every bad call is refused; asan and tsan flavors.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 428-429, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A C host loads clips, plays and controls voices, sets listener and emitters, and renders the mix itself.
