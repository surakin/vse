# Maintenance / linux-known-issues / Day 07 — Audio, scripting and physics leftovers that Linux can fix

**Series:** [Known issues Linux can fix](../linux-known-issues.md)

## Tasks

- [x] `scripts/test-phase.sh --start-day` at the start; scoped runs (`--affected` plain, then asan and tsan when the day touches concurrency or lifetimes) at the end, quoting the script's `ran X of Y` line.
- [x] Audio effects: `SetParams` clamps NaN and out-of-range blocks as `effect.h` says (Phase 26 closing review); tests with NaN and infinity in every effect's block.
- [x] `MusicPlayer`: a slot reused after a track ends does not inherit the old track's state, and `Stop` cancels a queued start; tests with two quick tracks.
- [x] Audio bench gaps (Phase 26 Day 442): an occlusion ray bench for 1,000 emitters through `physics_bench --scene rays`, the memory per voice, effect and stream, and a 5,000-voice run (the pool tops out at 4,096: the test says what happens beyond); numbers recorded in `docs/guide/audio.md`'s cost paragraph.
- [x] Scripting (Phase 27 Days 455 to 472): the merge applies entity and transform commands in one batched structural pass per component type (Day 455); `audio.play` takes `position` or `entity` and the `AudioSource` component is script-visible (a design question first: the day writes a short spec and asks the user); `script_unload` for a script with no live coroutines (Day 471); each as its own sub-task with a test, or an entry left with the reason.
- [x] Physics (Phase 25 Day 412): the pair tracker's single thread and tall-stack behaviour stay recorded; the joint scan is made indexed (a per-body joint list) and measured; the stress tests run at the planned 100k bodies as `soak` tests.
- [x] Read the remaining Deferred entries of Phases 20 to 27 once, list in the day file which ones need a person, a Windows or a Mac machine (not fixed here), and which are Linux-fixable but not in this series (a follow-up series is proposed to the user, with the list).
- [x] Both compilers, unity on and off; public API documented (`docgen lint` zero, `docs` target, the guide pages); `/code-review high` on the day's diff, findings verified, fixed with a regression test or declined with a reason, recorded below.
- [x] `KNOWN-ISSUES.md` entries removed or rewritten, `STATUS.md` updated; commit and push.

**Done:** The listed audio, script and physics leftovers are fixed with tests or recorded again with a reason, and the day file holds a classified list of everything else in the Deferred section.

## Post-implementation code review

`/code-review high` on the day's diff: 2 findings, both nits, no change needed (the cancel of a not-yet-started track drops its queued actions on purpose; the joint index keeps stale slots until a compaction, bounded and checked at every use). One own-review fix before the report: `CrossFadeTo` no longer clears the layer slots (it would have left layer voices uncontrollable).

## Results

- **Effects:** `SetParams` of the biquad, one-pole, delay and reverb clamps every field (NaN to the nearest legal value, a stable denominator, delay and pre-delay inside their buffers, feedback and line gains below one, mix gains at most 16). Test: a block of NaN, infinity and 1e30 for every effect (fails without the fix).
- **MusicPlayer:** the current and layer slots are never handed out again (a `CrossFadeTo` after a short track had reused the old track's slot and faded the new one away); `Stop` and `CrossFadeTo` cancel a track whose start is still queued and later; a mixer `QueueFull` re-queues the action one block later. Two tests, both fail without the fix. The queue-full retry has no test (it needs a full command ring).
- **Benches:** `physics_bench --scene occlusion` (1,000 tests 2.4 ms, 2.4 us each), `audio_bench --scene memory` (1.5 KiB per pool slot, hall reverb 170 KiB, 1 s delay 375 KiB, stream buffer 33 KiB) and `--scene limit` (pool refuses 5,000; 4,096 mixed, 904 stolen, 13.2 ms a block); numbers in `docs/guide/audio.md`.
- **Scripting:** the batched merge was measured (`script_bench --scene merge`: 12 to 13 percent of a tick, 100 ns per transform command) and not built; `ScriptRuntimeStats::merge_nanoseconds` added. `audio.play` placement: a spec with three options and a recommendation, waiting for the user (`docs/superpowers/specs/2026-11-24-script-audio-placement.md`). `script_unload` left: it needs a new C function (an ABI minor, one of the open user decisions) and cannot undo the globals a script defined in each partition state.
- **Physics:** `DestroyBody` finds a body's joints through a per-body list (a derived index, rebuilt by `Restore`): 20,000 bodies with 20,000 joints destroyed in 4.4 ms instead of 593 ms (`physics_bench --scene joint-destroy`). The per-pair joint scan the guide still described was already a hash set (container audit Day 7); the guide is corrected. The pair tracker's single thread and the tall-stack behaviour stay recorded. The stress scenes have soak forms (100,000 bodies for 100 steps, 27 s Release; 10^7 steps of a spinning body, 3 s).
- **Deferred entries of Phases 20 to 27, classified** (a read of every bullet by a subagent, checked in part): the full list is in "Classified deferred entries" below.
- Runs: plain `ran 1143 of 12332 tests (phases: 3 25 26 27 28; flavors: plain; display: excluded; crash tests: excluded; soak: excluded)`, asan `ran 1113 of 12336`, tsan `ran 913 of 12340`, all passed. Clang and GCC, unity on and off built clean (audio, physics, script and bench targets); `docgen lint` 100 percent, `ctest -L docs` 13 of 13.

## Classified deferred entries

**Needs a person, the Windows PC or the Mac:** the Phase 27 Days 473-475 and 477 items (Windows LuaJIT observations, the `engine_c_runtime_asan` hang, the unwatched script demo, the Mac builds); Phase 26 Days 440, 441 (WASAPI and CoreAudio loss paths, locked console, the Mac's cost bound), Day 415 shared-library build, Day 442 backends on the other machines; Phase 25 Days 385, 386, 407-411 and the closing runs (arm64, the PC's and the Mac's hashes, no Release tree on the PC); Phase 20 Day 278 (Windows limits); Phase 27 Day 444 (arm64).

**Linux-fixable, not in this series (a follow-up series is proposed to the user), by value:**
1. The Phase 7 crash tests that fail on Linux in plain, asan and tsan (`tests/debug/assert_family_test.cpp:498`, the ENGINE_ASSERT test, the tsan stack-overflow test, `renderer_demo_crash_after_reset`): `engine/foundation/debug`, small to medium.
2. `engine_c_import_shared*` failing or hanging in an importer-off shared tree: `engine/engine_c`, small to medium.
3. GCC Release `-Werror=stringop-overflow` at `containers/vector.h` (from `settings/config_text.cpp`) and the GCC `engine/net` `-Wsubobject-linkage` stop: small each (check whether still true: the GCC tree builds the audio, physics and script targets here).
4. The Phase 27 closing review's findings (about 40 sandbox and determinism items): `engine/script`, `script_physics`, `engine_c`; large as a whole, small each.
5. The Phase 25 Day 413 review findings (first-256-triangle mesh test, GJK/EPA thresholds, hull winding check, CCD and `collide_connected`, live `Joint` edits, duplicate feature ids in a snapshot, destroying a body ends no events): `engine/physics`.
6. The Phase 26 Day 443 review findings (snapshot float and cycle checks in `Mixer::LoadState`, `audio_render` lock, `audio_close` waiting, `SpatialAudio::Drop`, `Reconfigure` effect state, virtual voice `Pause`, long `Fade` snapshots): `engine/audio`, `engine_c`, medium.
7. Phase 25 and 27 smaller items: contact events of a character capsule naming entity 0, `physics.threads` through the C API, mesh queries allocating per call, `script_task_run` synchronous, `script_call` entity form, persistence items of Day 469, the audio snapshot, clip-blob and C API fuzzers, an audio-thread allocation counter.
8. Verification runs: the blessed physics hashes of Days 394, 395 and 411 on Linux (`physics_tests "[hash]"` passes in the runs above).

**Accepted limits:** LuaJIT `pairs` order and budgets checked every 1,000 instructions, libm in script math, tsan on the uninstrumented unwinder (now suppressed), the `script_bench_check` bounds and the hostile suite's length, the resting-contact spring of the solver, the pendulum's 1.2 percent loss, no gyroscopic term, debug draw without a renderer pass, no LeakSanitizer on macOS, the OS facts (locked console, Windows connect timeout).
