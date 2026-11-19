# glTF coverage / Day 9: weight tracks in clips (`ANIM` v2) and their playback

**Series:** [glTF coverage](../gltf-coverage.md) (decision 6)
**Depends on:** Day 8.

## Tasks

- [x] **Format** (`asset/clip.h/.cpp`): a new section of weight tracks (per track: a target key naming what it animates, the target count,
  constant or keyed, keys `UNorm16` per weight between a per-track minimum and extent, as the scale channel does); the skeleton becomes
  optional (a clip with weight tracks only has no skeleton dependency and joint count 0). **Type version 2**; version 1 reminted offline.
  Decide and write down the target key (the glTF node's name? an index into the level's entities?) so a clip binds to the entity that has
  the `MorphWeights`.
- [x] **Build:** `BuildClipBlob` reduces and quantizes weight keys within a new bound (`assets.anim_weight_error`, default 1/1000), measured
  on the finished blob as the TRS channels are.
- [x] **Playback** (`engine/animation`): `AnimationPlayer` on an entity with `MorphWeights` samples the clip's weight track for it each
  tick into the component (linear between keys); blending of two clips (if the player blends TRS) blends weights the same way.
- [x] **Tests:** build and view; the error bound holds; a weights-only clip binds without a skeleton; a clip with both drives a skinned,
  morphed entity; the hostile-blob test covers the section; deterministic bytes; `remint`.
- [x] **Standing rows.**

## Notes

Done on the PC (2026-11-19). **No tests were run** (the user's request; they run after Day 12); only `build-msvc` was built.

- **No version bump (as on Days 4 and 6).** A version-1 reader requires the skeleton dependency, so it refuses a weights-only clip;
  a skinned clip with a weight section still opens there, without its weights. The version stays 1.
- **The weight track:** section 20, `kClipSectionWeights`: the target count, the key count and 8 zero bytes, then the times, then the
  values key by key. **Stored as `f32`, not reduced or quantized** (a change to the plan, which asked for `UNorm16` keys within a new
  `assets.anim_weight_error` bound: the reduction machinery is per TRS channel, and weights are few). `ClipBuildInput::weights`;
  `ClipView::WeightTargetCount`, `WeightKeyCount`, `SampleWeights` (linear, clamped at the ends); the reader checks every time and
  value.
- **The target is the player:** a clip's weight track animates the entity that plays it, so it needs no node name. A clip may have
  joint tracks and a weight track (a skinned, morphed mesh) or a weight track only. Then it has no skeleton and no dependency, and binds
  to no skeleton (a skeleton player refuses it, counted).
- **`MorphWeights` moved to the animation module** (`animation::MorphWeights`, id 43; Day 8's notes): the animation system writes it,
  and `animation` cannot include `scene`.
- **Playback:** the skeleton query samples the weight track into an entity's `MorphWeights` when it has one. A second query plays
  weights-only clips on entities with `MorphWeights` and no `SkeletonInstance`, advancing time as the skeleton path does. Not done:
  cross-fade of weights (the fade blends poses only), and events of weights-only players (the event query asks for a skeleton).
- Tests (written, not run): `clip: a weight track without a skeleton round-trips and samples linearly`. Not written: the system's
  sampling into `MorphWeights`, hostile weight sections.
- Docs: `asset/clip.h`, `animation/components.h`; `docs/reference` regenerated.

**Done when:** a clip can animate morph weights, with or without a skeleton, and the animation system plays it into `MorphWeights`.
