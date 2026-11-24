# Maintenance / linux-known-issues / Day 07 — Audio, scripting and physics leftovers that Linux can fix

**Series:** [Known issues Linux can fix](../linux-known-issues.md)

## Tasks

- [ ] `scripts/test-phase.sh --start-day` at the start; scoped runs (`--affected` plain, then asan and tsan when the day touches concurrency or lifetimes) at the end, quoting the script's `ran X of Y` line.
- [ ] Audio effects: `SetParams` clamps NaN and out-of-range blocks as `effect.h` says (Phase 26 closing review); tests with NaN and infinity in every effect's block.
- [ ] `MusicPlayer`: a slot reused after a track ends does not inherit the old track's state, and `Stop` cancels a queued start; tests with two quick tracks.
- [ ] Audio bench gaps (Phase 26 Day 442): an occlusion ray bench for 1,000 emitters through `physics_bench --scene rays`, the memory per voice, effect and stream, and a 5,000-voice run (the pool tops out at 4,096: the test says what happens beyond); numbers recorded in `docs/guide/audio.md`'s cost paragraph.
- [ ] Scripting (Phase 27 Days 455 to 472): the merge applies entity and transform commands in one batched structural pass per component type (Day 455); `audio.play` takes `position` or `entity` and the `AudioSource` component is script-visible (a design question first: the day writes a short spec and asks the user); `script_unload` for a script with no live coroutines (Day 471); each as its own sub-task with a test, or an entry left with the reason.
- [ ] Physics (Phase 25 Day 412): the pair tracker's single thread and tall-stack behaviour stay recorded; the joint scan is made indexed (a per-body joint list) and measured; the stress tests run at the planned 100k bodies as `soak` tests.
- [ ] Read the remaining Deferred entries of Phases 20 to 27 once, list in the day file which ones need a person, a Windows or a Mac machine (not fixed here), and which are Linux-fixable but not in this series (a follow-up series is proposed to the user, with the list).
- [ ] Both compilers, unity on and off; public API documented (`docgen lint` zero, `docs` target, the guide pages); `/code-review high` on the day's diff, findings verified, fixed with a regression test or declined with a reason, recorded below.
- [ ] `KNOWN-ISSUES.md` entries removed or rewritten, `STATUS.md` updated; commit and push.

**Done:** The listed audio, script and physics leftovers are fixed with tests or recorded again with a reason, and the day file holds a classified list of everything else in the Deferred section.

## Post-implementation code review

(To be filled in when the day is done.)
