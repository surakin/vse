# Phase 25 / Day 398 — Windows: Build, Tests and the Cross-Machine Hashes

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 397; Phase 13.

On the Windows PC: everything builds and runs, and the state hashes equal Linux's.

## Tasks

- [x] Pull and build with `windows-msvc` (plain and asan) and `windows-clang-cl` in both unity modes; the CMake flags of Day 374 (`/fp:precise`, no contraction) are verified in the compile lines of both compilers (`cmake/` toolchain files).
- [x] Run the physics tests with `scripts/test-phase.sh --affected` (Git Bash) and the closing set for phase 25 files; the `physics_determinism` ctest compares against the committed hashes of Day 395.
- [x] A hash mismatch is investigated to a cause (a flag, an intrinsic, a libm difference in `Atan2`), the fix goes into shared code (a non-exact operation changed or the flag corrected) and Linux is rerun for the fix; a libm-only difference is recorded in `KNOWN-ISSUES.md` with the scene and step where it first differs.
- [x] The C API host tests of Days 396-397 and the sample run on Windows (the DLL load path of Phase 23 Day 354).
- [x] Windows-specific code lives under `*/windows/` (nothing physical needs it; the check is that nothing leaked); `BUILD.md` unchanged unless a prerequisite appeared.
- [x] Record the final `ran X of Y` line per flavor and the hash table (scene, Linux, Windows) in this file; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push from the PC.

**Done:** The Windows PC builds and passes the physics tests, and every reference scene's hash equals Linux's or its difference is explained in `KNOWN-ISSUES.md`.

## Outcome

Done 2026-11-16 on the Windows PC (AMD Radeon 860M), first at `abe06e22` (Day 397), then again after pulling the Mac's Days 399-400 (`609d6c67`).

**Builds.** `build-msvc`, `build-windows-msvc-nounity`, `build-clang-cl`, `build-windows-clang-cl-nounity`, `build-msvc-shared` and
`build-clang-cl-shared` build clean after four fixes:
- `engine/physics/joint.cpp`: a local `kPi` hid `debug_draw.cpp`'s in the unity build (cl's C4459, an error under `/WX`). Both files now use
  `math/math_util.h`'s `kPi`, and `WrapAngle`'s `kTwoPi` is `2.0f * kPi`: the same floats as the literals they replace (every hash unchanged).
- `tests/physics/collide_test.cpp`: `__builtin_inff()`, which cl does not have, became `Limits<f32>::Infinity()`.
- `tests/physics/joint_test.cpp`: `1.0f / 0.0f`, which cl refuses as a constant division by zero (C2124), became `Limits<f32>::Infinity()`.
- `cmake/EngineCompilerOptions.cmake`: clang-cl failed on every file of ours, because `/fp:precise` is Clang's `-ffp-model=precise`, a model that
  contracts, and overriding it with `/clang:-ffp-contract=off` is `-Woverriding-option`, an error under `/WX`. clang-cl now gets Clang's own
  switches, as on Linux (`/clang:-ffp-contract=off /clang:-fno-fast-math`); cl keeps `/fp:precise`. Every object file of ours in `build-msvc`
  carries `/fp:precise` and every one in `build-clang-cl` carries both switches (457 and 343 compile lines, none without; third-party code keeps its
  own flags).

**Hashes.** `fp_determinism_probe` prints `hash 50e1be9251ea408f contraction=off denormals=kept` under cl and clang-cl. At `abe06e22` every committed
hash equalled Linux's but two, the compound and mesh contact hashes (`8dd025d0c667077f` and `0f9c69146c8427d3` here, the same under cl and clang-cl,
against Linux's `33059fc50145b4c9` and `716573da705d5b3c`). Their random samples used `Sin` and `Cos` (`Quaternion::FromAxisAngle`, the mesh's
height field), while the hashes whose samples use `+ - * /` and `Sqrt` only were equal, and the physics code on those paths calls no trigonometry:
the C libraries' last bit, not the engine. (Computing the tests' `Sin` and `Cos` in double gave a third value, `8cfa6a9e8ff6707f`, as on the Mac.)
The day made the two samples libm-free; meanwhile the Mac had found the same thing and pushed its own fix (`2d334fdd`, then its review's), so at
the pull the day dropped its version and took the Mac's. Its constants are equal here.

| Hash | Linux | Windows (cl and clang-cl) | the Mac |
|---|---|---|---|
| `fp_determinism_probe` | `50e1be9251ea408f` | equal | equal |
| golden Stack, Pyramid, Pile, Bridge, Walker, Bullet (every thread count) | the table in `golden_test.cpp` | equal | equal |
| 100000 random pairs (inputs, outputs) | `ab187a6cda5f0dfb`, `a37013a6ea09b3fe` | equal | equal |
| 30000 hull pairs | `172dfbee2611621e` | equal | equal |
| 600-step scene | `25109949efc555a6` | equal | equal |
| joint, hinge first-step | `a2253470c556a2d0`, `f159eb48d1b6d124` | equal | equal |
| compound contacts (old, libm sample) | `33059fc50145b4c9` | `8dd025d0c667077f` | `a15eea1f351d3783` |
| mesh contacts (old, libm sample) | `716573da705d5b3c` | `0f9c69146c8427d3` | `647b245934c92d2c` |
| compound contacts (Day 399: inputs, outputs) | not run | equal | `a9fbbe81c8cac993`, `c9a286265fa0bdc1` |
| mesh contacts (Day 399: inputs, outputs) | not run | equal | `437a68043615cd54`, `1b49078840a2d8a9` |
| C API physics sample checksum | `532bfea0` | `532bfea0` | `532bfea0` |

**C API.** `engine_c_physics`, `engine_c_physics_joints` and `engine_c_physics_sample` pass plain and asan, statically linked, and through the DLL
(`build-msvc-shared` `engine_library` 107 of 107, `build-clang-cl-shared` 90 of 90, after the pull). Nothing physical needed Windows code: no file
under `engine/physics` or `tests/physics` names Win32 or `ENGINE_PLATFORM_WINDOWS`; `BUILD.md` unchanged.

**Runs, at `abe06e22` with the day's changes** (`scripts/test-phase.sh --affected --no-build`; every phase, `cmake/` changed):
- `build-msvc` plain: `ran 3388 of 6533 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 343 s`, one failure
  (`Throughput scales with participant count`, passed 3 of 3 alone);
- `build-windows-msvc-nounity` plain: `ran 3388 of 6533 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 472 s`, two
  failures: `engine_c_runtime` timed out at 300 s (passed 3 of 3 alone) and `Resets in a loop on a real device ...` failed after 137 s (below);
- `build-clang-cl` plain: `ran 3418 of 3574 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 485 s`, all passed;
- `build-windows-clang-cl-nounity` plain: `ran 3418 of 3574 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 596 s`,
  one failure (`Skewed load: other participants execute stolen work`, passed 3 of 3 alone);
- `build-msvc` asan: `ran 2862 of 6533 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 371 s`, two failures: the
  compound and mesh hash tests' asan binary had not been rebuilt after a constant change (`--no-build`); after the rebuild they passed;
- `build-windows-msvc-nounity` asan: `ran 2862 of 6533 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 286 s`, all
  passed.

**The reset loop froze the PC.** Rerun alone (`ctest --repeat until-fail:3`), `Resets in a loop on a real device ...` (phase 8) was still running
after more than 6 minutes; the desktop stopped responding while the PC answered pings, and it was restarted. Not run again, not examined
(`KNOWN-ISSUES.md`).

**After the pull (Days 399-400):** all six trees rebuilt clean; `ctest -L "^(phase25|phase18)$"` passed 607 of 607 in each MSVC tree (plain and
asan) and 305 of 305 in each clang-cl tree; the shared trees' `engine_library` as above; `ctest -L docs` 7 of 7; the gates (`*_check`, `*_lists`,
`test_phase*`) 84 of 84. The full `--affected` runs were not repeated after the pull (the reset loop is in them).

## Post-implementation code review

`/code-review high` over the day's diff, done inline: 4 findings. **Fixed (1):** the day's own mesh sample hash left out the grid's heights (fixed
in the day's version, then superseded by the Mac's tests, which hash them). **Recorded (3):** (1) the hinge angle calls `Atan2` (libm) every
substep, the one inexact operation of the step (`docs/guide/physics.md` says so); every hinge and bridge hash is equal here, so nothing was
changed (changing it would mean an exact angle without libm, which the plan does not do); (2) the control probe that shows the no-contraction
check can fail is skipped under cl and clang-cl (`KNOWN-ISSUES.md`, Day 374); (3) the regenerated compound and mesh constants are not run on Linux
yet (`KNOWN-ISSUES.md`, Days 382, 383 and 399).
