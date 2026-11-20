# glTF colliders / Day 8: the PC and the Mac (the user's machines)

**Series:** [glTF colliders](../gltf-colliders.md)
**Depends on:** Day 7.

## Tasks

- [ ] **The PC** (built 2026-11-20; plain MSVC tests pass and the hash is equal; asan and clang-cl not run, the user's choice; see Notes) (`build-msvc` and `build-clang-cl`, both unity modes): build clean; `scripts/test-phase.sh --affected` (plain and asan on
  MSVC) over the series' range; `host_character_world` and the physics sync tests pass; the character scene's hash equals Linux's.
- [x] **The Mac** (`build-macos`, unity only, plain only, per the standing rules; `build-macos-arm64` compiles): the same tests; the hash
  equals Linux's. **Done without the arm64 tree** (not built: the user's rule of 2026-11-20).
- [ ] **Records** (the Mac's half done, 2026-11-20): the `ran X of Y` lines here; `KNOWN-ISSUES.md`: remove the series' "not run on the PC and the Mac" entries that pass;
  `STATUS.md`; commit and push from each machine.

## The Mac (2026-11-20)

- **Built:** `build-macos` from scratch (the tree had been removed; unity, x86-64, Debug), clean. **Not built:** `build-macos-arm64` and
  the unity-off trees (the user, 2026-11-20: "stop arm64 and nonunity builds"); no sanitizer flavors (the user's standing rule).
- **Run:** `scripts/test-phase.sh --affected --since f12b7d52 --display` (the Mac's last commit, so the run also covers the commits the
  Mac had not run since: the skinned-model fixes and the test binaries the PC and the Mac left out; `CMakePresets.json` and
  `engine/engine_c/CMakeLists.txt` made it run everything):
  `ran 4233 of 4440 tests (phases: all; flavors: plain; display: included; crash tests: excluded; soak: excluded) in 2288 s`.
- **The character scene's hash equals Linux's:** `host_character_world: resting height 0.9100`, `position hash 81970852f4f13a9f`
  (`engine_c_character_world` passes). The physics sync tests, `gltf_collider_test.cpp`, `mesh_collider_test.cpp`, `level_test.cpp`,
  `level_spawn_test.cpp`, `host_import.c` and `host_runtime.c` pass. Also passing: the pose-culling tests of the skinned-model fixes
  (`tests/renderer/skinning_test.cpp`), `animation_tests` (plain), `docs_script_api` and `input_probe_runs_cocoa`.
- **Failed, 12** (`KNOWN-ISSUES.md`): the known voice-cost snapshot and the known hostile-scripts short form; four docgen `Samples:` tests
  (19 of 19 pass alone right after: flaky under the full run's load); four CoreAudio `Backend:` tests (`device->Start()` returns
  `IoError` after about 5.7 s, again 4 of 10 when rerun alone; the backend has not changed since Phase 26 Day 441); `test_phase_labels`
  and `test_phase_script_build_plan_asan`, which fail on this tree because it never built the sanitizer flavors (the first sees the
  `*_NOT_BUILT-*` placeholder tests Catch2's discovery registers for unbuilt asan and tsan binaries, the second finds no asan tests to
  select).
- **No code changed on the Mac**, so there is no end-of-day review (the day's only edit besides the records is the memory note of the
  build rule).

**Done:** the series builds and passes on Windows and macOS, and the character hash is equal on the three machines.

## Notes (2026-11-20, the PC)

- **Built clean:** `build-msvc`, `build-clang-cl`, `build-windows-msvc-nounity` and `build-windows-clang-cl-nounity`. MSVC (unity) refused
  `engine_c/runtime.cpp`: the level spawn's `ColliderSource made` hid the loop's `made` vector (C4456, which clang-cl and GCC do not
  report); renamed `made_collider`.
- **Tests, plain `build-msvc` only** (the user: "run only plain msvc tests"; asan and clang-cl not run): ctest over the labels `asset`,
  `physics`, `engine_library` and `boundary`, without soak, crash and sanitizer tests or the reset loop: `100% tests passed out of 762`
  (15 skipped: the shared-library programs, not in this tree, and the fused-probe control) in 60 s. `engine_c_character_world` prints
  `position hash 81970852f4f13a9f`, **equal to Linux's**.
- The Mac's half is not started.
