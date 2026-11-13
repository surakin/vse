# Phase 22 / Day 334 — glTF Import II: Skeletons & Animation

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 318 (skeleton), 319 (mesh skin), 320 (clips), 333 (the reader and the import manifest).

Skins become skeletons, animations become clips, and skinned meshes get their skeleton reference.

## Tasks

- [ ] **Skins to skeletons:** each glTF `skin` becomes one skeleton resource. Joints are ordered parent-first (the skin's own order when it already is, otherwise a stable topological order by node index); a non-joint ancestor between two joints folds its transform into the child's bind-local transform (refused when the product has shear that translation, rotation and scale cannot hold, naming the node); the accumulated transform above the root joint stays in the root's bind-local transform so the skeleton is in the glTF scene's space.
- [ ] **Names:** a joint takes its node's name; a missing name becomes `joint_<node index>`, a duplicate gets a deterministic `_<n>` suffix, a name over 64 bytes is cut and gets a hash suffix; the renames are listed in the import report (the clip import below binds by node, not by name).
- [ ] **Inverse bind matrices:** taken from the skin's `inverseBindMatrices` accessor when present (written as `ibm` overrides, Day 318), computed from the bind pose otherwise; a stored matrix that differs from the one computed from the nodes by more than 1e-4 is a counted warning, a matrix that is not affine is refused; a skin with more than 256 joints is refused with a message naming the limit and the skin.
- [ ] **Skinned meshes:** a mesh whose primitives use a skin gets the skeleton id, hash and joint count (the dependency of Day 319); the `JOINTS_0` node indices are remapped to the skeleton's joint order; two meshes sharing a skin share the skeleton; primitives of one mesh using different skins are refused.
- [ ] **Animations to clips:** each glTF `animation` yields one clip per skeleton it animates (named `<animation>_<skin>`); `translation`, `rotation` and `scale` channels on joint nodes become tracks; channels on nodes outside the skeleton and `weights` channels are dropped with a counted warning listing the node. Samplers: `LINEAR` keys map to keys, `STEP` becomes a pair of keys at the step (the later one one tick after, documented), `CUBICSPLINE` is resampled to linear keys at `--clip-rate` (default 30 Hz) with the error the resampling made measured and recorded; quaternions get sign continuity and the hemisphere rule; the clip minter of Day 320 (key reduction and quantization) then applies, so the declared error bounds are those of the final blob, and the measured resampling error is added to the import report.
- [ ] **Clip metadata:** duration from the last key time over all channels; `--loop NAME` marks a clip looping (glTF has no loop flag); no events (glTF has none); root motion is not extracted (Day 321's option on the clip, off).
- [ ] **Hostile files:** sampler input and output counts that disagree, non-increasing or NaN times, a channel targeting a missing node, a skin joint that is not a node, a cycle in the node tree, an inverse-bind accessor of the wrong count; each refused naming the object.
- [ ] **Tests** (fixtures from `gltf_writer.h`): a 5-joint chain with a non-joint node between two joints (the skeleton's model-space bind pose equals the glTF's world transforms within 1e-5); a 20-joint humanoid with an animation of all three channels sampled by the Day 321 evaluator at 1,000 random times against a double-precision reference sampler of the glTF's own interpolation, within the declared bounds; a cubic-spline channel within its recorded resampling error; STEP behaviour; two skins and two meshes; a skinned mesh minted against its imported skeleton loads and `CheckSkeleton` passes; byte-identical output twice; re-import keeps the ids of skeletons and clips.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (the importer parses hostile files); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. Importer code that is not behind the JSON or image-decoder wrappers uses the engine's types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 338; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels; `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/assets.md` gets the "Importing glTF" section (this phase's three days add to it) and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** the importer lives in `tools/asset_mint/gltf/` and is never linked into the engine or a game (the Day 336 gate checks it); a new target or edge updates `ARCHITECTURE.md` and `tools/module_deps_check/modules.txt` in the same commit; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a skinned, animated glTF imports to skeleton, mesh and clip blobs that the Day 321 evaluator plays back within the declared error of the glTF's own animation, with stable ids.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
