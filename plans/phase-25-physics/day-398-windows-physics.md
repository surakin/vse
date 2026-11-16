# Phase 25 / Day 398 — Windows: Build, Tests and the Cross-Machine Hashes

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 397; Phase 13.

On the Windows PC: everything builds and runs, and the state hashes equal Linux's.

## Tasks

- [ ] Pull and build with `windows-msvc` (plain and asan) and `windows-clang-cl` in both unity modes; the CMake flags of Day 374 (`/fp:precise`, no contraction) are verified in the compile lines of both compilers (`cmake/` toolchain files).
- [ ] Run the physics tests with `scripts/test-phase.sh --affected` (Git Bash) and the closing set for phase 25 files; the `physics_determinism` ctest compares against the committed hashes of Day 395.
- [ ] A hash mismatch is investigated to a cause (a flag, an intrinsic, a libm difference in `Atan2`), the fix goes into shared code (a non-exact operation changed or the flag corrected) and Linux is rerun for the fix; a libm-only difference is recorded in `KNOWN-ISSUES.md` with the scene and step where it first differs.
- [ ] The C API host tests of Days 396-397 and the sample run on Windows (the DLL load path of Phase 23 Day 354).
- [ ] Windows-specific code lives under `*/windows/` (nothing physical needs it; the check is that nothing leaked); `BUILD.md` unchanged unless a prerequisite appeared.
- [ ] Record the final `ran X of Y` line per flavor and the hash table (scene, Linux, Windows) in this file; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push from the PC.

**Done:** The Windows PC builds and passes the physics tests, and every reference scene's hash equals Linux's or its difference is explained in `KNOWN-ISSUES.md`.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
