# Phase 8.5 / Day 124e — The Container Gate, Docs, Closing Full Run & Wrap-up

**Phase:** [Phase 8.5 — Standard Types and Container Hygiene](../phase-08-5-types-and-containers.md)
**Depends on:** Days 124a-124d.

Keep the code clean, state the rule, run everything, close the phase.

## Tasks

- [ ] **The gate** `tools/std_container_check` (a ctest `std_container_check`, like `raw_new_check` and `file_access_check`): fails on a banned `std::` name (the list from the spec) or an `#include` of its header in `engine/`, `game/` and `tools/` outside an allow list with a **reason per entry** (the `shader_mint` JSON boundary file, a
      Catch2 adapter header, third-party code); comments and string literals are ignored; a stale allow-list entry fails; if Day 124a decided to gate raw integer spellings (`unsigned char`, `uint8_t`, `long long`, ...) it checks them too with the API-boundary list. Self-tests with planted violations (each banned name, an include, a raw spelling) and a clean
      fixture, as the other gates have.
- [ ] The gate also covers the Phase 8 and later test files by pattern (`tests/io/`, `tests/types/`, `tests/settings/` and any directory added later), with the old test directories listed as one allow-list entry each, with the counts, so the list only shrinks.
- [ ] Update `CLAUDE.md`: the existing "STL containers/algorithms … remain unused" sentence becomes a hard constraint with the exact scope, the vocabulary (`byte`, `i8`..`i128`, `u8`..`u128`, `Span`), the exceptions and a pointer to the gate; `PLAN.md` assumption #2 gets the same; `STATUS.md` commands; Phase 9's day files get a note that new code in `tests/settings/` uses the engine's types from the start.
- [ ] **Guide pass:** `docs/guide/foundation-containers.md` and the new "Standard types" section read end to end as a new developer (a "Which type do I use?" table at the top: bytes, text, sequences, maps, owned pointers, views, 128-bit); every tagged sample compiles and runs; `docs/guide/README.md` current; `docgen lint` zero; reference regenerated and committed.
- [ ] Known-issues sweep: the old tests' deferral (counts per directory), anything the ports could not convert (with the reason), the Windows build of the portable 128-bit path (unverified beyond the cross-syntax check until Phase 10); entries this phase fixed removed.
- [ ] **Closing full run (CLAUDE.md, required for a phase's last day):** `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, both demos 1000 frames on Wayland and X11 with `--validation`; record the final `ran X of Y tests (phases ...)` line in this file; nothing left out.
- [ ] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (Days 124a-124e as committed: from the commit before Day 124a to the working tree); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [ ] Update `plans/phase-08-5-types-and-containers.md` (status complete, days marked done), `PLAN.md` (roadmap row, progress-log entry), `STATUS.md`; `CURRENT_PHASE` stays 8.5 until Phase 9's Day 125 starts (policy); commit.

**Done:** the gate passes with a documented allow list, `CLAUDE.md` states the type vocabulary and the container rule, the closing line `ran X of Y tests (phases: all; flavors: all)` is recorded with every test passing, the demos behave on both display backends, and Phase 8.5 is marked complete.
