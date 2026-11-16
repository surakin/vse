# Phase 24 / Day 372 — Docs, Closing Runs on Three Machines & the Phase Review

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 358-371.

Documents the phase, runs everything everywhere, and reviews the whole phase.

## Tasks

- [ ] Guide `docs/guide/engine-library.md`: "Scenes, cameras and lights", "Animated models", "Live textures", "Labels" and the host's duties (ordering of the frame's systems, threads, DPI); the spec updated to what was built; the coordinate conventions in one table.
- [ ] **Linux:** `scripts/test-phase.sh --all --flavor all` (plain, asan, tsan), `ctest -L docs`, the demos the phase ships, the shared and the static builds, the importer-off build; the final `ran X of Y` lines recorded here.
- [ ] **Windows PC and Mac:** the closing runs there as well (`windows-msvc` plain and asan, `windows-clang-cl`; `macos-clang` plain, asan, tsan and the arm64 compile), shared and static; or recorded as open in `KNOWN-ISSUES.md`.
- [ ] ABI: the snapshot and `abi_check` record the phase's additions; the version follows the spec's rule; the C header compiles as C11 with `-pedantic -Werror`.
- [x] **Uncapped end-of-phase review (CLAUDE.md):** `/code-review high --max-findings all` over the whole phase's diff (Days 358-371); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [ ] **Records:** `plans/phase-24-c-api-scenes-for-hosts.md` status, `PLAN.md` (roadmap row, log, a new assumption on scenes for hosts), `CLAUDE.md` (the C API bullet gains the scene rules), `STATUS.md` (next: Phase 25 needs its day files before it starts), the memory notes, the `KNOWN-ISSUES.md` sweep (no shadows, no orthographic cameras unless built, no zero-copy host GPU textures, no per-vertex morph targets, limits filed by the days).

**Done:** the phase is closed on all machines with every run quoted, the review is recorded, and `STATUS.md` names Phase 25 as next.

## Outcome

(written when the day is done)

## Post-implementation code review

Uncapped `/code-review high --max-findings all` over the whole phase (`e1d33fae` to the working tree, 9790 diff lines),
run as three parallel reviewers by area; 36 candidate findings in all (8 renderer/textures, 14 importer/tests, 14
runtime/surface). Verified against the code and **fixed**:

- `query` accepted the LABEL bit but the loop stopped at the animation player, so a label-only mask returned every entity
  (`runtime.cpp`); regression check in `host_scene.c`.
- `level_spawn` used the rotation without normalising it (`component_set` does); now normalised.
- `engine_frame_stats`, `engine_surface_state` and `engine_mesh_reference` grew in place and a Phase 23 host got
  `STRUCT_SIZE`; they now accept the first-version size (32, 56, 16) and write or read only `struct_size` bytes
  (`kFrameStatsFirstSize` ... in `handles.h`); regression check in `host_scene.c` (old-size structs, nothing written
  past them).
- The before-present hook ran on a lost device (`renderer.cpp`); now skipped.
- `LiveTextures`: a resize between the size read and the copy could upload a mismatched image (the copy now checks the
  size it copied); self-move in `EndFrame` guarded.
- Level import: depth bound one too loose against the spawner's own root (`kMaxLevelDepth` 64 to 63, guide updated);
  translation or scale outside f32 refused instead of converting to infinity; name uniquifier passes 4 to 32 before its
  quadratic fallback.

**Declined or deferred, with reasons** (the rest are listed in `KNOWN-ISSUES.md`): a node with zero scale or a shear
refuses the import (documented behaviour, a hidden object is not a level); `LiveTextures` is not registered with
`DeviceResources` and binds by raw handle (reset of a surface goes through the surface rebind, which rebuilds the scene;
a `Renderer::ApplySettings` reset of a surface's own device is not supported by the C API); render under the world mutex;
`attach_world` after propagation; a replaced skeleton keeps its animation player; the surface managers are not in the
memory backend's in-use query; the "skipped counts as pass" regexes and the vacuous spike assertions; no unit tests for
`ImportLevel` hostile cases; `SurfaceScene::Rebind` unused; stale `surface_state` counters after unbind; fade of a
one-shot clip; `HostTexture` raw counters pointer.

Tests: `scripts/test-phase.sh --affected` plain after the fixes: `ran 3379 of 8931 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 139 s`; the two failures were the X11Window pointer-grab and mapped-window tests (flaky family) and `docs_check`, which was stale generated docs, regenerated after. Full run before the review: `ran 8931 of 8931 tests (phases: all; flavors: all; display: included; crash tests: included) in 220 s`, failures only those two X11Window tests. The new regression checks were written with the fixes; that they fail without them was not demonstrated.

