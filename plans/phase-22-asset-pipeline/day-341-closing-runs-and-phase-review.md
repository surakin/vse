# Phase 22 / Day 341 — Closing Runs on Three Machines & the Phase Review

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 313-330.

Close the phase.

## Tasks

- [ ] (the Linux half done by the full run; the Mac and the Windows PC open) **Skeletal assets in the closing runs (added 2026-11-13):** the new `engine/animation` module, the packed vertex formats (Day 317, with the DX12 and Metal mapping and readback tests) and the skinned paths (Day 322) are exercised on all three machines; the `.metallib` and DXIL outputs of the skinned shader are regenerated or verified on the Mac and the Windows PC; every waiting entry of Days 317 and 322 in `KNOWN-ISSUES.md` is closed or restated.
- [x] **Closing full run on Linux (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --all --flavor all`, plus `ctest -L docs` and the demos; quote the final `ran X of Y tests (phases ...)` line here; `CURRENT_PHASE` stays 22 until Phase 24's first day.
- [ ] (open: not run on this machine; `KNOWN-ISSUES.md`) **Windows PC and Mac:** the closing runs there as well (`windows-msvc` plain and asan, `windows-clang-cl`; `macos-clang` plain, asan and tsan, plus the arm64 compile check); the texture and mint determinism tests run on every machine and the byte-identical golden hashes must match across them (a mismatch is a bug in the minter, not in the test); or recorded as open in `KNOWN-ISSUES.md`.
- [x] **Uncapped end-of-phase review (CLAUDE.md):** `/code-review high --max-findings all` over the whole phase's diff (Days 313-341, from the commit before Day 313 to the working tree); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [x] **Records:** `plans/phase-22-asset-pipeline.md` status, `PLAN.md` (roadmap row, log), `STATUS.md` (next: Phase 24 needs its day files before it starts), `KNOWN-ISSUES.md` sweep.
- [x] **Runs and records (CLAUDE.md):** quote each run's final `ran X of Y tests (phases ...)` line in this file; `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the whole suite passes in every flavor on Linux (and on the other machines, or their runs are recorded as open), the phase review is recorded, and Phase 24 is next.

## Outcome

Phase 22 is closed on the Linux machine (2026-11-15). The closing run: `scripts/test-phase.sh --all --flavor all`: `ran 8813 of 8813 tests (phases: all; flavors: all; display: included; crash tests: included)`: one test failed, `test_phase_script_affected_from_git`, because `game/CMakeLists.txt` had no line in `tests/module_map.txt` (fixed in this commit; the test script family, 43 tests, passes again). `ctest -L docs`: 7 of 7. The whole tree builds clean with Clang and GCC non-unity (every target); GCC unity fails only in `engine/net` (known). The demos run inside the suite. **Windows PC and Mac: not run** (this was the Linux machine): what they must run is the closing set of Phase 22 (the minters' byte-identical golden hashes, the vendored C libraries, the skinned shaders' `.metallib` and DXIL, the new tests): open in `KNOWN-ISSUES.md`.

## Post-implementation code review

The uncapped review (`/code-review high --max-findings all`) of the phase's diff (Days 313-340, from `9f3b3743~1`): **17 findings, 6 fixed, 11 recorded** (the 10 numbered in the `KNOWN-ISSUES.md` Day 341 entry, plus the manifest `#` case inside them). Fixed: a hand-indexed `argv` in `asset_demo` (`cmdline_access_check` was red at HEAD: the gate caught it, the review confirmed it), the resource server using a connection id after a failed reply dropped the connection, the network client trusting the server's `max_read_bytes` (now clamped to 4 KiB to 1 MiB), the back-off shift that could wrap (now a saturating doubling at both sites), a cast of an out-of-range double in the importer's error message, and a handle used before its validity check in the level spawner. Recorded with the reason it was not changed here: the possible mount-table deadlock with a network mount (needs a test of its own before a fix), the unchecked mesh indices and skin joints on the upload path (a design decision of Day 319, `verify_on_load` and the tools check them), the follower-of-a-cancelled-leader case, the blob cache's cost and trust, the fallback key-0 and blob-size-limit mismatches, the server's directory rescans, the skeleton hash that is trusted, an unnormalized boundary rotation, the buffer pool's power-of-two accounting and `#` in a source path.
