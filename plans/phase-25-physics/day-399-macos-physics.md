# Phase 25 / Day 399 — macOS: Build, Tests and the Cross-Machine Hashes

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 397; Phase 21.

On the Mac: everything builds and runs, and the state hashes equal Linux's and Windows'.

## Tasks

- [x] Fetch and build with `macos-clang` (plain; asan and tsan are skipped on the Mac by the user's standing instruction, `KNOWN-ISSUES.md`) in both unity modes; `macos-clang-arm64` compiles in both unity modes (compile-only: nothing runs, so arm64's results are unchecked and listed in `KNOWN-ISSUES.md`).
- [x] Run the physics tests through `scripts/test-phase.sh` (bash 3.2) and the `physics_determinism` ctest against the Day 395 hashes; the x86-64 hashes must equal Linux's and Windows'.
- [x] arm64 note: FMA contraction on Apple clang is off by the Day 374 flag; the day records that the flag is present in the arm64 compile line, since the result cannot be run.
- [x] A mismatch is handled as on Day 398 (shared fix, rerun of the other machines, libm-only differences recorded).
- [x] The C API tests and the sample run on macOS with the Metal backend where they render; window tests need the console unlocked.
- [x] Format with MacPorts' `clang-format` 23.x; record the `ran X of Y` line and the three-machine hash table here; `STATUS.md`, `KNOWN-ISSUES.md`; commit and push from the Mac.

**Done:** The Mac passes the physics tests plain, the x86-64 hashes equal Linux's and Windows', and the arm64 compile is clean.

## Outcome

Done on the Mac (2026-11-16, `master` from `abe06e22`, Intel, macOS 15.6, Apple clang from the Command Line Tools), plain only (asan and tsan skipped by the user's standing instruction).

**Builds.** `build-macos` (unity) and `build-macos-nounity` build clean; `build-macos-arm64` and `build-macos-arm64-nounity` compile clean (compile-only). Two build fixes in shared CMake, both found by the arm64 trees, which had not been built since Phase 23 Day 350:
- `tests/engine_library/CMakeLists.txt`: Day 350's build-time `asset_mint` run (the resource host's blobs) ran the arm64 tool on the Intel Mac (`Unknown system error -86`, bad CPU type). In an `ENGINE_COMPILE_ONLY` tree `engine_resource_data` is now an empty target; the hosts still compile.
- `tests/physics/CMakeLists.txt`: the fused control probe added `-mfma` from `CMAKE_SYSTEM_PROCESSOR`, which an arm64 tree on an Intel Mac reports as `x86_64` (`clang++: error: unsupported option '-mfma'`). The root `CMakeLists.txt` now sets `ENGINE_ARCH` (`x64` or `arm64`, the CMake side of `build_defs.h`'s `ENGINE_ARCH_*`) and the probe reads it.

**arm64 FMA note.** In `build-macos-arm64`, the engine's and the physics module's compile lines carry `-arch arm64 ... -ffp-contract=off -fno-fast-math`; the control probe adds `-O2 -ffp-contract=fast` (no `-mfma`). Nothing arm64 was run (no Apple-silicon Mac), so no arm64 hash is known (`KNOWN-ISSUES.md`, Day 374).

**Hashes.** On the first run 209 of 211 phase-25 tests passed; the two failures were the compound and mesh contact hashes (`a15eea1f351d3783` and `647b245934c92d2c` here against Linux's `33059fc50145b4c9` and `716573da705d5b3c`). Every hash whose inputs use no trigonometry matched; those two built their random rotations (and the mesh grid's heights) with `sinf`/`cosf`, and the physics code on those paths calls none. Computing the tests' trig in double and rounding gave a third value (`8cfa6a9e8ff6707f` for the compound), so no libm here is the "correct" one to copy: the inputs were made trig-free instead (`testing::RandomTurn`, `RandomTurnAbout`, `Wave` in `tests/physics/test_values.h`; `RandomTurn` is the hull test's former `HRandPose` rotation, whose hash is unchanged), each test now hashes its sample apart from its contacts (as `collide_test.cpp` does), and the four constants were blessed here (pushed early as `2d334fdd` so the PC's Day 398 runs them; the review then changed them again). **The Linux machine and the Windows PC have not run the new constants.** The review also made `MakeHingeJoint`'s anti-parallel half turn exact (`FromAxisAngle(x, pi)` gave `w = -4.4e-8`); no committed hash changed here.

| Hash | Linux | Windows | the Mac (x86-64) |
|---|---|---|---|
| `fp_determinism_probe` | `50e1be9251ea408f` | Day 398 | `50e1be9251ea408f` |
| golden Stack, Pyramid, Pile, Bridge, Walker, Bullet (every thread count) | the table in `golden_test.cpp` | Day 398 | equal |
| 100000 random pairs (inputs, outputs) | `ab187a6cda5f0dfb`, `a37013a6ea09b3fe` | Day 398 | equal |
| 30000 hull pairs | `172dfbee2611621e` | Day 398 | equal |
| 600-step scene | `25109949efc555a6` | Day 398 | equal |
| joint, hinge first-step | `a2253470c556a2d0`, `f159eb48d1b6d124` | Day 398 | equal |
| compound contacts (old) | `33059fc50145b4c9` | Day 398 | `a15eea1f351d3783` (differs: libm inputs) |
| mesh contacts (old) | `716573da705d5b3c` | Day 398 | `647b245934c92d2c` (differs: libm inputs) |
| compound contacts (new: inputs, outputs) | not run | not run | `a9fbbe81c8cac993`, `c9a286265fa0bdc1` |
| mesh contacts (new: inputs, outputs) | not run | not run | `437a68043615cd54`, `1b49078840a2d8a9` |
| C API physics sample checksum | `532bfea0` | Day 398 | `532bfea0` |

**Tests.** Phase 25 plain: 212 of 212 in `build-macos` and in `build-macos-nounity` (with the new hinge test). The C API hosts (`engine_c_physics`, `engine_c_physics_joints`, `engine_c_physics_sample`) are headless (nothing renders, so no Metal) and pass statically linked and through the dylib (`build-macos-shared`: `engine_library` plain 82 of 82; `engine_c_host_cpp` and `engine_install_consumer` pass, the first Mac run of the C++ layer). Scoped run after the review fixes (`scripts/test-phase.sh --affected --flavor plain`, `build-macos`): `ran 3432 of 9415 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 471 s`, all passed (an earlier run before the fixes: `ran 3431 of 9412`, all passed); the skipped tests need a shared-library tree and were run in `build-macos-shared`. `ctest -L docs` not run: no public header or guide changed. Formatted with MacPorts' `clang-format` 23.1.5 (`scripts/format.sh`).

## Post-implementation code review

`/code-review high`. The first run was pointed at commit `abe06e22` by mistake and reviewed Day 397's code (`engine/engine_c/runtime.cpp`) instead of this day's: its 10 findings are not this day's and were not verified or fixed here; they are listed in `KNOWN-ISSUES.md` (Day 399) for the Linux machine. The second run, over `abe06e22..` and the working tree, found 9:
1. Stale `KNOWN-ISSUES.md` entries still said the Mac had not compared hashes it had (Days 374, 378, 379, 381, 394-397, and the C++ layer): **fixed**.
2. The new constants were blessed on the Mac alone: **declined** as a defect (the plan's shared fix needs a value from somewhere; the hull, golden and pair hashes show the collision code agrees across the machines), **recorded** in `KNOWN-ISSUES.md` for the Linux machine and the PC.
3. `MakeHingeJoint` built its half turn with `FromAxisAngle(x, pi)` (libm in the engine): **fixed** with the exact quaternion; regression test `Hinge: an axis against the frame's Z turns the frame by an exact half turn (no libm)`, which fails without the fix (`w == -0.000000044f`).
4. The compound and mesh hashes had no inputs hash to tell sample from code: **fixed** (both hash their sample apart).
5. The probe re-derived the target processor on its own: **fixed** with the root's `ENGINE_ARCH`.
6. A second trig-free rotation recipe beside `HRandPose`: **fixed** (`testing::RandomTurn`, used by the hull, mesh and compound tests).
7. `half_cos` drawn uniformly clustered the turns near half a turn: **fixed** (random normalized pairs and four-vectors instead).
8. The mesh sanity test kept the old `Sin`/`Cos` grid: **fixed** (both tests use `WavyHeight`).
9. The comment's list of allowed operations left out `Abs` and `FMax`: **fixed** (the comment and `KNOWN-ISSUES.md` say exact operations).
