# glTF coverage / Day 9: weight tracks in clips (`ANIM` v2) and their playback

**Series:** [glTF coverage](../gltf-coverage.md) (decision 6)
**Depends on:** Day 8.

## Tasks

- [ ] **Format** (`asset/clip.h/.cpp`): a new section of weight tracks (per track: a target key naming what it animates, the target count,
  constant or keyed, keys `UNorm16` per weight between a per-track minimum and extent, as the scale channel does); the skeleton becomes
  optional (a clip with weight tracks only has no skeleton dependency and joint count 0). **Type version 2**; version 1 reminted offline.
  Decide and write down the target key (the glTF node's name? an index into the level's entities?) so a clip binds to the entity that has
  the `MorphWeights`.
- [ ] **Build:** `BuildClipBlob` reduces and quantizes weight keys within a new bound (`assets.anim_weight_error`, default 1/1000), measured
  on the finished blob as the TRS channels are.
- [ ] **Playback** (`engine/animation`): `AnimationPlayer` on an entity with `MorphWeights` samples the clip's weight track for it each
  tick into the component (linear between keys); blending of two clips (if the player blends TRS) blends weights the same way.
- [ ] **Tests:** build and view; the error bound holds; a weights-only clip binds without a skeleton; a clip with both drives a skinned,
  morphed entity; the hostile-blob test covers the section; deterministic bytes; `remint`.
- [ ] **Standing rows.**

## Notes

(written during the day)

**Done when:** a clip can animate morph weights, with or without a skeleton, and the animation system plays it into `MorphWeights`.
