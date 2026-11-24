# Phase 28 / Day 505 — Docs, Tutorials, Closing Runs and the Phase Review

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 478-504.

Close the phase.

## Tasks

- [x] Tutorials and the mental-model introduction (CLAUDE.md: they wait for the phase's last day): "Lighting a World with a Sky" (procedural to cube map to reflections), "Making a Landscape" (a height map to a walkable, textured terrain), "A Forest in One Draw" (instances and hierarchies, the cost model: chunks, runs, subtrees), compiling samples in `docs/samples`; glossary terms (cube map, height map, splat map, tile, level of detail, geomorph, chunk, instance run, subtree); `find-your-way.md`; the guides `docs/guide/renderer.md`, `terrain.md`, `instancing.md`, `physics.md`, `engine-library.md`, `scripting.md` complete and current; `docs/reference` regenerated; **no phase or day numbers in any published page** (`docs_phase_check`).
- [x] `PLAN.md` (roadmap row to complete, progress log, new assumptions for the sky and cube texture, terrain, instancing and hierarchy), `CLAUDE.md` (bullets: "Terrain and the height-field collider are one data set", "Instances are chunked sets and packed trees, never one entity each"), `ARCHITECTURE.md`, `BUILD.md`, `STATUS.md`, `KNOWN-ISSUES.md`, `tests/module_map.txt` and `tools/module_deps_check/modules.txt` for any new target, the memory notes: all current.
- [x] **The phase's last day runs the whole suite in every flavor:** `scripts/test-phase.sh --all --flavor all`, `ctest -L docs` and the demos (`render_features_demo` in a window with validation on Wayland and X11, 1,000 frames) on Linux; the Windows and macOS runs are Days 502 and 503's; record every `ran X of Y` line here.
- [x] **The end-of-phase review is uncapped (CLAUDE.md):** `/code-review high --max-findings all` over the whole phase's diff (from the commit before Day 478 to the working tree), with extra scrutiny on the instance set's id and chunk bookkeeping, the hierarchy's array edits (`subtree_size` invariants), the terrain's untrusted blob parsing, the height-field collider's determinism and the lifetime of every new GPU owner across a reset; verify, fix with a regression test shown to fail without the fix, or decline with a reason; record the count and the outcome under "Post-implementation code review".
- [x] `CURRENT_PHASE` stays 28 until the next phase's first day changes it (Phase 29, the first playable demo, once its day files exist).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** Phase 28 is documented, the whole suite passes in every flavor on Linux, the Windows and macOS days are recorded, and the uncapped review's findings are resolved.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").
- Done 2026-11-24 (Linux). Tutorials: `lighting-a-world-with-a-sky.md`, `making-a-landscape.md`, `a-forest-in-one-draw.md` (each with a compiled, run sample; the docgen sample link list gained `physics`), the glossary terms (cube map, height map, splat map, tile, level of detail, geomorph, chunk, instance run, subtree), `find-your-way.md`, the tutorials README. The guides were already current from Days 478-504. `PLAN.md` assumptions 33 (terrain) and 34 (instancing) and the roadmap row; `CLAUDE.md` two bullets; a memory note.
- **Closing run:** `scripts/test-phase.sh --all --flavor all`: `ran 12253 of 12253 tests (phases: all; flavors: all; display: included; crash tests: included; soak: included) in 859 s`; 4 failed, all recorded before (three X11 window tests and `Snapshot: the cost of a voice ...`). That run was made on the tree after the review's fixes except the physics sync test; after it the sync tests (`Sync:` 48 of 48), the docs target (`docgen` lint at zero, 14,574 of 14,574 documented) and `ctest -L docs` passed, and the Clang unity, Clang unity-off, GCC unity and GCC unity-off trees were rebuilt. The windowed demo ran 1,000 frames with validation on Wayland and X11 and with device resets (`render_features_window_{wayland,x11,wayland_reset}`, in that run). Windows (Day 502) and macOS (Day 503) were the user's runs.

## Post-implementation code review

The uncapped review (`/code-review high --max-findings all`, run as four read-only reviewers over the phase diff `fcc5187f..HEAD`, by area: terrain, instancing and scenes, physics and spatial, sky, RHI, C API and scripts) reported **45 findings** (12 + 10 + 11 + 12). Each was checked against the code.

**Fixed, 16** (regression tests, each shown to fail without the fix where marked; the rest are covered by the existing suites):
- An abort through `environment.set` (the stored sky was asserted valid at the frame): the surface now falls back to the flat colour when the procedural sky cannot be drawn.
- A height map header with an extreme range or cell size (NaN heights, overflowing tiles): refused on open and mint (range within 10,000 km, cell size 0.001 to 100,000 m); the height field refuses a cell size under 0.001. Tests: `height map: a range or a cell size that would overflow ...`, `HeightField: a cell size below a millimetre is refused`.
- Unused terrain layers carrying NaN poisoned the material: layers beyond the count are reset in the pass.
- `InstanceTree::Attach` dropped a root's offset: test `attaching a root keeps its offset` (fails without the fix).
- A stand-in beyond the cull distance brought back a culled node: refused by the LOD check; test `a stand-in beyond the cull distance is refused`; the random cull test no longer generates it.
- A stale LOD state when another tree takes a render-scene slot: reset by the tree's identity.
- A hostile `Scatter` rule (a count of four billion, or coordinates of 1e300): refused up front; test in `instance_set_test`.
- A ray through a cell's diagonal or vertex could miss both triangles: a 1e-5 edge slack; test `a vertical ray never slips through ...`. This moved the demo's tree placement by one node (55,895 to 55,896 visible, the visible hash updated in `game/render_features_demo/CMakeLists.txt` and the guide).
- The physics sync ignored a changed hull, mesh, compound or height-field id: compared now; test `a collider that differs only in a library id is applied again` (fails without the fix).
- Wind marked every node dirty each frame: a node whose rotation did not change is skipped.
- The instanced rotation check accepted a squared length from 0.5 to 1.5: now 0.98 to 1.02.
- A failed sky environment cube was rebuilt every frame: tried once per setting.
- Messages and doc comments (the tile-drop warning, `QueryBox`'s rows, the cell size bound).

**Declined or recorded, 29** (`KNOWN-ISSUES.md`, "Phase 28 Day 505", numbered there): the tile-level guarantee with tall tiles, the tile drop order past 4,096 tiles, the unverified pyramid and the mip drift, the pins that outlive a replaced map, `terrain_height` for a parentless-world or scaled terrain, the out-of-memory abort, the contact limits of a large body, a body wholly under the terrain, the ridge push-out, the snapshot's height-field identity, the `TerrainHeight` scan, the quadratic held lists of the two sync systems, the loose bounds after `Detach`/`Attach`, the wind rotation replacing an authored one (documented), the chunk re-rounding and memory amplification, the instance-set tint on a transform call, the half-float face check, the Vulkan cube limit and the ABI minor version. Each needs a design change or a larger test than a closing day allows. Three reviewer claims were not reproduced as stated: the ABI snapshot's missing slots (the `engine_abi_snapshot` test passes on the generator's output), the missing registry for the surface's terrain source (it is rebuilt on a rebind by design) and the `Narrow` overflow (every path is bounded by `kMaxInstanceCoordinate` and `kMaxExtent`).

## The Windows PC's closing runs (2026-11-24)

Days 504 and 505 and the maintenance series "known issues Linux can fix" came after Day 502, so the PC ran the phase's closing set afterwards, at `35b013ce` plus the fixes below. The tests ran through ctest, every test but the phase 8 reset loop (the user's standing rule; `scripts/test-phase.sh --all` cannot leave one test out). Crash, soak and display tests were included.

- **Builds:** the seven Windows trees build clean: `build-msvc`, `build-clang-cl`, both non-unity trees and the three shared trees. One MSVC fix was needed: `tests/engine_library/host_terrain.c` declared two inner `back`s that hid the outer one (C4456).
- **`build-msvc`, plain and asan:** `99% tests passed, 2 tests failed out of 8455` in 840 s. The failures were the flaky `Throughput scales with participant count` and the crash-handler abort test; both passed 3 of 3 alone.
- **`build-clang-cl`:** `99% tests passed, 2 tests failed out of 4603` in 399 s. One failure was the flaky resolver queue test, which passed 3 of 3 alone. The other was `test_phase_script_build_plan_asan`: the clang-cl tree builds no asan tests, as before.
- **Shared trees:** `build-msvc-shared` 8452 of 8455, `build-clang-cl-shared` 4601 of 4603, `build-msvc-shared-noimport` 8429 of 8435.
  - Every failure but one passed alone: the known flakes and one asan timeout under load (`KNOWN-ISSUES.md`).
  - The exception was `engine_c_import_shared`, which hung for 900 s in the importer-off tree. That tree's library has no importer, so every import answers UNSUPPORTED, and `TestAsync`'s poll loop never saw `done`. Linux made the same registration fix in parallel (`5661e717`), and the rebase kept Linux's version: the test exists only when the library has the importer, and that tree checks UNSUPPORTED with `engine_c_import_off`. The loop now stops on a failed poll, and with a failure line after about a minute.
- **The render features demo:** its 9 tests pass on Windows: `render_features_demo_hash_1/2/4/16`, `render_features_screenshot`, `render_features_window_win32`, `render_features_window_win32_reset`, `render_features_bad_sky` and `render_features_reset_needs_window`.
- **Host texture layouts:** Direct3D 12 samples a `BGRA8` texture natively (`ENGINE_FORMAT_NATIVE`). `engine_c_import` and `engine_c_glass_screen` pass.
- **Review:** `/code-review high` on the PC's diff gave 2 findings, both fixed. The failed-poll return now releases the ticket, and the poll loop has a bound.