#pragma once

//! Importing glTF images, textures and materials.
//!
//! Phase 22 Day 338. Images (a URI, a `data:` URI or a buffer view) decode to RGBA8; each glTF texture becomes one
//! texture resource per way it is used (sRGB color for the base color, linear data for the normal map; BC7 block
//! compression, mip chain) minted by [[asset::MintTextureFromPixels]]; each glTF material becomes one material resource
//! of the standard G-buffer template, minted from a generated `.mat.src` by [[asset::MintMaterial]]. What the template
//! cannot hold (a metallic-roughness, occlusion or emissive texture, an emissive color, a texture transform) is dropped
//! with a counted warning naming the material, or refused when it changes what the surface is (a
//! `KHR_texture_transform`). `KHR_materials_unlit` sets the material's unlit flag (`renderer::kMaterialFlagUnlit`).

#include "asset_import/gltf_container.h"
#include "asset_import/gltf_nodes.h"

namespace vse::asset
{

/// Imports the textures and the materials of a parsed glTF.
///
/// Appends the texture resources (path `<prefix>/textures/<name>`, `<name>_normal` for a normal map) and then the
/// material resources (path
/// `<prefix>/materials/<name>`), dependencies first, with their manifest entries, to `result`.
///
/// - Param document: a parsed glTF.
/// - Param graph: its node graph (which materials skinned meshes use).
/// - Param source: reads the images the glTF names by URI.
/// - Param options: limits, ids, manifest, path prefix.
/// - Param types: the registry the blobs are minted with.
/// - Param result: receives the resources, manifest entries and warnings (appended to).
/// - Param error: receives the sentence naming the glTF object.
/// - Fails: `Invalid` (an image that does not decode, a texture without an image, an index outside its array),
/// `Unsupported` (a texture transform that is not the identity, a material the template cannot take), `TooLarge` (an
/// image above the
///   pixel limit), `NotFound` (an image file the source lacks), `LimitExceeded` (memory).
/// - Thread-safety: thread-safe for separate calls with separate arguments.
/// - Ownership: borrows its arguments for the call only; `*result` owns what it receives.
Status ImportMaterials(const GltfDocument& document, const GltfNodeGraph& graph, GltfSource& source,
                       const ImportOptions& options, const TypeRegistry& types, ImportResult* result, GltfError* error);

}  // namespace vse::asset
