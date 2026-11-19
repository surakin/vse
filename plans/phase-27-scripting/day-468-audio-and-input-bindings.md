# Phase 27 / Day 468 — Audio and Input Bindings

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 450, 452, 459; Phases 15, 26.

Scripts make sound and read the player.

## Tasks

- [x] Audio: `audio.play(clip_id, {gain, pitch, bus, loop, position, entity})` returning a voice handle, `audio.stop/pause/set_gain/set_pitch(voice)`, `audio.music.play/crossfade/stop`, `audio.set_bus_gain`, `AudioSource`/`AudioListener` through the component bindings; all `world-write` through the command buffer, applied at the sync point and posted to the audio command queue in the merge order (so the audio repeatability of Phase 26 holds: the same script run gives the same block-stamped commands at any thread count).
- [x] Voice handles are generation-checked values; a stopped voice's handle is an error value on use; `on_voice_end` events through the bus.
- [x] Input: `input.key_down/pressed/released(key)`, `input.mouse_position/delta/button`, `input.axis(name)`, `input.action(name)` (the Phase 1/12 action mapping), gamepads where the platform layer has them; all `pure` reads of **the input snapshot taken at the tick start** (identical in every partition), and text input and `on_input` events through the bus.
- [x] Input recording for replay: the snapshot per tick is what the replay log of Day 469 stores, so a recorded session reruns the same script behavior.
- [x] Tests: a script plays a clip and the block-stamped command stream equals at 1 and 16 threads, handle errors, input snapshots identical in 64 partitions, replayed input gives the same script decisions.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

## Notes

- Target: the new `script_media` (`engine/script_media`, layer 14, `script`, `audio`, header-only `platform_input`), mirroring `script_physics`; service slots `kServiceAudio = 1` and `kServiceInput = 2` in `script/service_slots.h`; command kinds 120-127.
- Audio: `audio.play/stop/pause/resume/set_gain/set_pitch/set_bus_gain/play_music/crossfade_music/stop_music` (all `world-write`) and `is_playing`/`exists` (`world-read`). The plan's `audio.music.*` is `audio.play_music` etc.: the registry has no dotted namespaces. A voice handle is `(tick + 1) << 32 | reserved placeholder index`: it exists before the voice, depends only on the script's own calls, and the merge binds it to the mixer's `VoiceId` in `VoiceTable`; a stopped or ended handle is a script error; `PumpAudioEvents` turns `Finished`/`Stopped`/`Stolen` into `on_voice_end` for the starting entity. `audio_listener` is a registered component; `AudioSource` and the `position`/`entity` options are deferred (`KNOWN-ISSUES.md`).
- Input: the host's `InputSnapshot::Capture(tick, os::InputState, text, InputMap, externals)` at the tick start; `input.*` reads (keys, edges, mouse, scroll, text, named actions and axes); `EmitInputEvents` puts changes on the bus as `on_input`; `Serialize`/`Deserialize` are the replay record Day 469 stores. There is no action-mapping system in the platform layer (the plan text is wrong), so the host fills an `InputMap`; no gamepad type exists, so sticks come in as `ExternalAxis`.
- Tests (`tests/script_media/script_media_test.cpp`, 14 cases): handle and voice start, every option error, handle errors after stop and end, the listener component, a world with no audio or input, key/edge/mouse/action/axis/text reads, host axes and map refusals, the record round trip and its refusals, a voice end reaching a script, 128 players at 1, 2, 3, 7 and 16 threads giving the same hash of rendered blocks, the same scene twice, 256 readers in 64 partitions agreeing, a replay of 24 recorded ticks giving the same decisions, input events.
- Found on the way: a first version of the scene ran out of mixer voices (a tick that stops a voice and starts another needs both until the next block) and the refused plays left handles that reported "has ended"; the mixer in the test has 1024 voices, and the behavior is documented.
- Pages touched: `docs/guide/scripting.md` ("Audio and input"), `docs/reference/script_media.md`, `script-api.md`, `script.md`, `symbols.txt`, `modules.txt`, `lint-baseline.txt`.

## Runs

- Phase 27 plain: `ran 276 of 11237 tests (phases: 27; flavors: plain; display: excluded; crash tests: excluded) in 29 s`
- Phase 27 asan: `ran 269 of 11237 tests (phases: 27; flavors: asan; display: excluded; crash tests: excluded) in 18 s`
- Phase 27 tsan: `ran 269 of 11237 tests (phases: 27; flavors: tsan; display: excluded; crash tests: excluded) in 19 s`
- Reduced runs, not the whole suite. Gates 105/105 of the selected `check`, `test_phase_script` and `module_deps` tests except `docs_check` (run again after the docs were regenerated: `ctest -L docs` 9/9); docgen lint at zero (`script_media` baseline 0 0). Builds clean: Clang unity, Clang non-unity (`build-nu`), GCC non-unity (`b_gcc`); the GCC unity tree stops in `engine/net` (known) before `script_media`. Windows, macOS and `-DENGINE_BUILD_SHARED=ON` not run.
- Two gate failures found on the way and fixed: the new test used `std::initializer_list` (`std_usage_check`; now `InitializerList`), and the root `README.md` of an earlier commit had no line in `tests/module_map.txt` (`test_phase_script_module_map_paths`; added `README.md -`).

## Post-implementation code review

`/code-review high`: 5 findings.
1. A voice handle was `(tick + 1) << 32 | index`, but a Lua state holds numbers as doubles (exact up to 2^53): after about 9.7 hours at 60 Hz the script's handle was rounded and every later call on it failed, and above tick 2^31 it overflowed. Fixed: a handle has 53 bits (31 of tick, 10 of partition, 12 of the partition's count; at most 4096 voices per partition per tick). Regression test `a handle survives the trip through a Lua number at any tick`, shown to fail without the fix (tick 2147483646: the play was refused).
2. Typed text cut at 1024 bytes could split a UTF-8 character: fixed (the cut moves back to a character boundary); test `text over the limit is cut between characters` (written with the fix; not run against the unfixed code).
3. `PumpAudioEvents` consumes every mixer event, including bus levels: declined, it is the documented contract of the call (the host forwards what it needs).
4. `input.key_*` scans the 95 key names with a `strlen` each: declined for now, to be measured on Day 476.
5. A host that never pumps keeps a table entry per voice: documented on `VoiceTable`.

**Done:** Scripts play and control audio and read input, with commands and snapshots identical for any thread count.
