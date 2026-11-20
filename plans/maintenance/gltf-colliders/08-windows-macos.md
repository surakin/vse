# glTF colliders / Day 8: the PC and the Mac (the user's machines)

**Series:** [glTF colliders](../gltf-colliders.md)
**Depends on:** Day 7.

## Tasks

- [ ] **The PC** (`build-msvc` and `build-clang-cl`, both unity modes): build clean; `scripts/test-phase.sh --affected` (plain and asan on
  MSVC) over the series' range; `host_character_world` and the physics sync tests pass; the character scene's hash equals Linux's.
- [ ] **The Mac** (`build-macos`, unity only, plain only, per the standing rules; `build-macos-arm64` compiles): the same tests; the hash
  equals Linux's.
- [ ] **Records**: the `ran X of Y` lines here; `KNOWN-ISSUES.md`: remove the series' "not run on the PC and the Mac" entries that pass;
  `STATUS.md`; commit and push from each machine.

**Done:** the series builds and passes on Windows and macOS, and the character hash is equal on the three machines.
