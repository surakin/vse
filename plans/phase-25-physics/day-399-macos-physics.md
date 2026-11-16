# Phase 25 / Day 399 — macOS: Build, Tests and the Cross-Machine Hashes

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 397; Phase 21.

On the Mac: everything builds and runs, and the state hashes equal Linux's and Windows'.

## Tasks

- [ ] Fetch and build with `macos-clang` (plain; asan and tsan are skipped on the Mac by the user's standing instruction, `KNOWN-ISSUES.md`) in both unity modes; `macos-clang-arm64` compiles in both unity modes (compile-only: nothing runs, so arm64's results are unchecked and listed in `KNOWN-ISSUES.md`).
- [ ] Run the physics tests through `scripts/test-phase.sh` (bash 3.2) and the `physics_determinism` ctest against the Day 395 hashes; the x86-64 hashes must equal Linux's and Windows'.
- [ ] arm64 note: FMA contraction on Apple clang is off by the Day 374 flag; the day records that the flag is present in the arm64 compile line, since the result cannot be run.
- [ ] A mismatch is handled as on Day 398 (shared fix, rerun of the other machines, libm-only differences recorded).
- [ ] The C API tests and the sample run on macOS with the Metal backend where they render; window tests need the console unlocked.
- [ ] Format with MacPorts' `clang-format` 23.x; record the `ran X of Y` line and the three-machine hash table here; `STATUS.md`, `KNOWN-ISSUES.md`; commit and push from the Mac.

**Done:** The Mac passes the physics tests plain, the x86-64 hashes equal Linux's and Windows', and the arm64 compile is clean.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
