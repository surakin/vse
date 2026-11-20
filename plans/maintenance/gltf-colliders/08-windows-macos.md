# glTF colliders / Day 8: the PC and the Mac (the user's machines)

**Series:** [glTF colliders](../gltf-colliders.md)
**Depends on:** Day 7.

## Tasks

- [ ] **The PC** (built 2026-11-20; plain MSVC tests pass and the hash is equal; asan and clang-cl not run, the user's choice; see Notes) (`build-msvc` and `build-clang-cl`, both unity modes): build clean; `scripts/test-phase.sh --affected` (plain and asan on
  MSVC) over the series' range; `host_character_world` and the physics sync tests pass; the character scene's hash equals Linux's.
- [ ] **The Mac** (`build-macos`, unity only, plain only, per the standing rules; `build-macos-arm64` compiles): the same tests; the hash
  equals Linux's.
- [ ] **Records**: the `ran X of Y` lines here; `KNOWN-ISSUES.md`: remove the series' "not run on the PC and the Mac" entries that pass;
  `STATUS.md`; commit and push from each machine.

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
