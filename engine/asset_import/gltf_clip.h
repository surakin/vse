#pragma once

//! Importing glTF animations as clips.
//!
//! Phase 22 Day 337. Each glTF animation yields one clip per skeleton it animates, named `<animation>_<skin>`:
//! translation, rotation and scale channels on joint nodes become tracks. `LINEAR` keys map to keys, `STEP` becomes a
//! pair of keys at the step (the later one a tick after), `CUBICSPLINE` is resampled to linear keys at the clip rate
//! and the error that made is measured and reported. Channels on nodes that are no joint, and `weights` channels, are
//! dropped with a counted warning. [[asset::BuildClipBlob]] then reduces and quantizes the keys.

#include "asset_import/gltf_skin.h"

namespace engine_placeholder::asset
{

/// Imports every animation of a parsed glTF as clips.
///
/// Appends the clips to `result->resources` (path `<prefix>/clips/<name>`), their ids to `result->manifest` and the
/// dropped channels and measured errors to `result->report`.
///
/// - Param document: a parsed glTF.
/// - Param graph: its node graph.
/// - Param options: limits, ids, manifest, path prefix, `clip_rate`, `looping_clips`.
/// - Param types: the registry the blobs are minted with.
/// - Param skins: the skeletons imported from the file's skins ([[asset::ImportSkins]]).
/// - Param result: receives the resources, manifest entries and report lines (appended to).
/// - Param error: receives the sentence naming the animation, channel or sampler.
/// - Fails: `Invalid` (a sampler whose input and output counts disagree, key times that are negative or not strictly
///   increasing, a channel without a target node, a node that does not exist, two channels for one joint and path, an
///   output of the wrong type, a quaternion of length zero), `TooLarge` (more keys than a clip channel holds),
///   `LimitExceeded` (memory).
/// - Thread-safety: thread-safe for separate calls with separate arguments.
/// - Ownership: borrows its arguments for the call only; `*result` owns what it receives.
Status ImportClips(const GltfDocument& document, const GltfNodeGraph& graph, const ImportOptions& options,
                   const TypeRegistry& types, Span<const ImportedSkin> skins, ImportResult* result, GltfError* error);

}  // namespace engine_placeholder::asset
