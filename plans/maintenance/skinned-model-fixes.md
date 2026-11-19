# A skinned model that did not open (maintenance, 2026-11-20, Linux)

Not a numbered phase. The user's `.glb` (a character exported through Sketchfab: one 237-joint skin, 13 skinned meshes, a root node
that scales by 0.0002 and turns 90 degrees, and one animation that holds the morph weights of two meshes at 0) stopped the model
viewer with "a clip of the file could not be loaded". Four engine bugs were behind it, and one problem is in the file.

## Fixed

- [x] **The clip loader** (`asset/cpu_loaders.cpp`) required a skeleton dependency of every clip; a clip of morph weights only (glTF
  coverage Day 9) has none, so the manager refused it. It now loads. Test: "manager: a clip of morph weights only loads without a
  skeleton" (failed without the fix).
- [x] **The animation system** (`animation/system.cpp`) played a weights-only clip only on entities without a skeleton; on a skinned
  mesh it took the skeletal path and skipped the entity every frame. Now the weights move and the skeleton stays in its bind pose
  (the step code is one helper, `StepPlayer`). Test: "animation: a clip of morph weights only plays on a skinned entity and on one
  without a skeleton" (failed without the fix).
- [x] **The joint palette's size** (`renderer/scene_frame.*`): `SceneFrame` made its `FrameResources` with the constructor's default,
  1024 matrices, and never read `renderer.joint_palette_bytes` (default 16 MiB). It now takes `SceneFrameSetup::max_joint_matrices`,
  else the setting from `initial_settings`, else 1024. The model viewer passes its settings in both paths.
- [x] **The skin's space** (`asset_import/gltf_skin.cpp`, `gltf_nodes.*`: `InvertAffineMatrix`): glTF places a skinned mesh by its joints
  alone, while the engine draws the entity's world transform times the palette; the skeleton held the transform above its root joints
  in the scene's space, so a scaled or turned root counted twice (this model: 0.0209 squared, drawn about 2,000 times too small). The
  root joints are now relative to the world transform of the node that uses the skin. `kGltfImporterOutputVersion` is 3 (fingerprint
  `0x1ba4a729f8cc6a6d`). Test: "gltf skin: joints are relative to the mesh node that uses the skin, so nothing above it counts twice"
  (failed without the fix).
- [x] **The model viewer**: a clip is given only to the entities that can play it (a skeletal clip to its skeleton's entities, a
  weights-only clip to those with weights); a skinned mesh no clip poses is drawn in its bind pose (the renderer refuses a skinned
  object with no palette); a clip that fails to load names the loader's reason.
- [x] **The framing** (asked by the user after the first commit): the view is framed on every mesh as it is drawn. A rigid mesh by its
  box under its transform (a morphed one by its vertices' own box, computed once at import: its blob bounds hold every target at full
  weight); a skinned mesh by each joint's bind-pose box (the blob's joint bounds) moved by that joint's palette matrix. The file's rest
  pose is not its bind pose: the joints' world times their inverse bind matrices is the mesh node's transform for the root and a plain
  scale for the others, so the drawn character stands while its bind-pose box lies down; framing on that box cut the head off.

## The file

Its two morphed meshes have 22 identical targets whose deltas are about 99 times the vertices' positions (a vertex at 81 units has a
delta of 8,071): a converter wrote scaled positions where deltas belong. At weight 0 (all the file's animation does) they change
nothing, but the bounds of those meshes hold every target at full weight (thousands of units), which framed the view on a box
thousands of times the model. The importer does not warn about such targets.

## Runs

- `scripts/test-phase.sh --affected --since HEAD`: `ran 1505 of 11521 tests (phases: 3 4 5 22 23 27; flavors: plain; display:
  excluded; crash tests: excluded; soak: excluded) in 51 s`; failures: `engine_c_surface` (known) and `docs_check` before the
  reference was regenerated.
- After it: `ctest -L docs` 9 of 9; the animation, skin, clip and weights-only cases under asan and tsan, 70 of 70.
- The user's model: opens with no warning, textured, in its bind pose (`--screenshot`), and after the framing change the whole
  character is in the picture. The viewer's ctests (screenshots of the robot, the meadow and a `.gltf`, bad files, the Wayland window)
  6 of 6.

## Post-implementation code review

`/code-review high`: 4 findings; 3 kept as designed, 1 recorded.
1. Programs that pass their settings now reserve the setting's default palette (16 MiB a frame slot): kept, it is the documented
   default, and 1024 matrices drew only part of one character.
2. A skinned entity playing a weights-only clip recomputes its bind pose every frame: kept, it is small, and caching it would need
   invalidating when the clip changes.
3. A second node using the skin at another place is drawn at the first one's place: kept, a skeleton has one space; a counted warning
   names it.
4. The framing leaves out the morphed meshes when others exist, so this model's head (its morphed meshes) is partly out of the
   picture: fixed by the framing change above (it was not the morphed meshes but the bind pose).

The framing change's review: 1 finding, recorded. The renderer culls a skinned object by its bind-pose mesh bounds under the entity's
transform (`SkinBounds`), so a file like this one, whose rest pose is not its bind pose, can be culled while on screen
(`KNOWN-ISSUES.md`); the joint bounds moved by the palette, as the viewer now frames, would be the fix.

Not run on the PC or the Mac.
