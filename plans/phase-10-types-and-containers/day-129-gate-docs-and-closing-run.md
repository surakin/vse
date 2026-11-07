# Phase 10 / Day 129 — The Container Gate, Docs, Closing Full Run & Wrap-up

**Phase:** [Phase 10 — Standard Types and Container Hygiene](../phase-10-types-and-containers.md)
**Depends on:** Days 125-128.

Keep the code clean, state the rule, run everything, close the phase.

## Tasks

- [x] **The gate** `tools/std_container_check` (a ctest `std_container_check`, like `raw_new_check` and `file_access_check`): fails on a banned `std::` name (the list from the spec) or an `#include` of its header in `engine/`, `game/` and `tools/` outside an allow list with a **reason per entry** (the `shader_mint` JSON boundary file, a
      Catch2 adapter header, third-party code); comments and string literals are ignored; a stale allow-list entry fails; if Day 125 decided to gate raw integer spellings (`unsigned char`, `uint8_t`, `long long`, ...) it checks them too with the API-boundary list. Self-tests with planted violations (each banned name, an include, a raw spelling) and a clean
      fixture, as the other gates have.
- [x] The gate also covers the Phase 9 and later test files by pattern (`tests/io/`, `tests/types/`, `tests/settings/` and any directory added later), with the old test directories listed as one allow-list entry each, with the counts, so the list only shrinks.
- [x] Update `CLAUDE.md`: the existing "STL containers/algorithms … remain unused" sentence becomes a hard constraint with the exact scope, the vocabulary (`byte`, `i8`..`i128`, `u8`..`u128`, `Span`), the exceptions and a pointer to the gate; `PLAN.md` assumption #2 gets the same; `STATUS.md` commands; Phase 11's day files get a note that new code in `tests/settings/` uses the engine's types from the start.
- [x] **Guide pass:** `docs/guide/foundation-containers.md` and the new "Standard types" section read end to end as a new developer (a "Which type do I use?" table at the top: bytes, text, sequences, maps, owned pointers, views, 128-bit); every tagged sample compiles and runs; `docs/guide/README.md` current; `docgen lint` zero; reference regenerated and committed.
- [x] Known-issues sweep: the old tests' deferral (counts per directory), anything the ports could not convert (with the reason), the Windows build of the portable 128-bit path (unverified beyond the cross-syntax check until Phase 12); entries this phase fixed removed.
- [x] **Closing full run (CLAUDE.md, required for a phase's last day):** `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, both demos 1000 frames on Wayland and X11 with `--validation`; record the final `ran X of Y tests (phases ...)` line in this file; nothing left out.
- [x] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (Days 125-129 as committed: from the commit before Day 125 to the working tree); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [x] Update `plans/phase-10-types-and-containers.md` (status complete, days marked done), `PLAN.md` (roadmap row, progress-log entry), `STATUS.md`; `CURRENT_PHASE` stays 10 until Phase 11's Day 130 starts (policy); commit.

**Done:** the gate passes with a documented allow list, `CLAUDE.md` states the type vocabulary and the container rule, the closing line `ran X of Y tests (phases: all; flavors: all)` is recorded with every test passing, the demos behave on both display backends, and Phase 10 is marked complete.


## Result

- `tools/std_container_check` (ctest `std_container_check` plus six self-tests with planted violations, a clean fixture, stale and reason-less allow lists); scans `engine/`, `game/`, `tools/` and all of `tests/`; the allow list names the types.h aliases, the shader_mint JSON boundary and its test, Catch2's printers, and the six older test directories with their line counts. `docs/guide/foundation-containers.md` links the "Which type do I use?" table; CLAUDE.md and PLAN.md assumption #2 state the rule; Phase 11's Day 130 carries the note for `tests/settings/`.
- Closing full run: `ran 3030 of 3030 tests (phases: all; flavors: all; display: included; crash tests: included)`; the only failure is the known `X11Window::Create opens a mapped window on the real X server` (KNOWN-ISSUES). `ctest -L docs`: 4 of 4 pass; docgen lint 3523/3523. `rhi_demo` and `renderer_demo`, 1000 frames with `--validation`: exit 0 on Wayland and on X11.

## Post-implementation code review

End-of-phase `/code-review high --max-findings all` over Days 125-129: 10 findings. Fixed: `shader_mint` cut the label at the first ".slang" (now only a trailing one); the gate skipped any `fixtures`/`generated` directory at any depth (anchored); an allow-list file covered by two entries marked only one used (all covering entries now count); the banned-name list lacked `swap`, `clamp`, `max_element`, `cout`, `getline`, `to_chars`, `exchange` and others (added); `Append` copied byte by byte (memcpy for trivially copyable types); `Sort` was a plain heap sort (now introsort, with tests for sorted, reversed, all-equal and organ-pipe input); the guide described the gate as scanning a "clean list" (it scans all of `tests/`); STATUS and day-file records written. Declined: a raw string literal spanning lines can show a false positive in the gate (none exist in the tree; a real one can be allow-listed); `StableSort`'s quadratic fallback when the scratch allocation fails (documented under `Fails:`); the removed null-span validation in `rhi/desc.cpp` (a null pointer with a size is a programming error and `Span` aborts consistently; the `rhi` tests that passed before still pass).
