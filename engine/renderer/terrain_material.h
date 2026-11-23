#pragma once

//! The terrain's surface: up to four texture layers blended by a splat map, a slope rule and a distance fade.
//!
//! [[renderer::TerrainMaterial]] is a plain value; the textures are shared with the program that made them. The layers
//! are one 2D array texture (`RGBA8_SRGB`: albedo in rgb, roughness in a), tiled over the terrain by a size in metres a
//! layer; the splat map is an `RGBA8_UNorm` texture across the whole terrain whose four channels are the weights of the
//! four layers. [[renderer::TerrainLayerWeights]] is the CPU twin of the weights the terrain fragment shader computes.
//! The terrain is not a mesh material: it has no emissive, no reflection override and no slot overrides.

#include "lang/scalar.h"
#include "math/vector.h"
#include "renderer/texture.h"
#include "types.h"

namespace vse::renderer
{

/// The most layers a terrain material blends.
inline constexpr u32 kMaxTerrainLayers = 4;

/// One layer of a terrain material.
///
/// - Thread-safety: thread-compatible plain data.
struct TerrainLayer
{
    f32 tiling = 8.0f;                   ///< Metres across one repeat of the layer's texture; above 0.
    Vec3 tint = Vec3(1.0f, 1.0f, 1.0f);  ///< Multiplies the albedo; not negative.
    f32 roughness = 1.0f;                ///< Multiplies the texture's roughness; 0 to 1.
    f32 metallic = 0.0f;                 ///< The layer's metallic value; 0 to 1.
};

/// What the terrain looks like.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: shares the two textures.
struct TerrainMaterial
{
    u32 layer_count = 1;  ///< The layers used, 1 to `kMaxTerrainLayers`; the splat channels from there on are ignored.
    TerrainLayer layers[kMaxTerrainLayers];  ///< The layers.
    /// The layer that steep faces use, or -1 for none. Where the surface leans more than the slope range, its weight
    /// replaces the splat weights.
    i32 slope_layer = -1;
    f32 slope_start = 0.35f;  ///< `1 - normal.y` at which the slope layer starts to take over; 0 to 1.
    f32 slope_end = 0.55f;    ///< `1 - normal.y` at which it has taken over; above `slope_start`, at most 1.
    f32 fade_start = 150.0f;  ///< Distance in metres at which the detail starts to fade into the layer's average
                              ///< colour.
    f32 fade_end = 300.0f;    ///< Distance at which it has faded; above `fade_start`.
    GpuTexturePtr
        layer_array;      ///< A 2D array of 2 to 4 layers, at least `layer_count`, `RGBA8_SRGB`; null draws flat grey.
    GpuTexturePtr splat;  ///< The weights, `RGBA8_UNorm`, one texel or more across the terrain; null is layer 0 alone.
};

/// Checks a material.
///
/// - Param material: the material.
/// - Returns: null if it is valid, else a static sentence naming the first broken rule (a layer count outside 1 to 4, a
///   tiling not above 0 or not finite, a tint or a scalar out of range, a slope or a fade range out of order, a slope
///   layer outside the layers, a layer array of the wrong format, kind or size (more than 4 layers or fewer than
///   `layer_count`), a splat map of the wrong format).
/// - Fails: never.
/// - Thread-safety: thread-safe.
/// - Ownership: borrows `material` for the call; the sentence is static.
const char* TerrainMaterialInvalid(const TerrainMaterial& material);

/// Rewrites four splat weights so they sum to exactly 255, keeping their proportions (largest remainder; a tie goes to
/// the lower channel). All zero becomes (255, 0, 0, 0).
///
/// - Param weights: the four weights, read and written.
/// - Fails: never.
/// - Thread-safety: thread-safe.
/// - Ownership: `weights` is written.
void TerrainNormalizeWeights(u8 weights[4]);

/// The weight of each layer at a point: the splat weights of the layers in use, replaced toward the slope layer by the
/// slope rule, summing to 1. The terrain fragment shader does the same.
///
/// - Param material: a valid material.
/// - Param splat: the four splat texel values, 0 to 1 (unnormalised input is renormalised).
/// - Param normal_y: the y component of the unit surface normal.
/// - Param out: set to the four layer weights; layers at or above `layer_count` get 0.
/// - Fails: never.
/// - Thread-safety: thread-safe.
/// - Ownership: borrows the arguments; `out` is written.
void TerrainLayerWeights(const TerrainMaterial& material, const f32 splat[4], f32 normal_y, f32 out[4]);

}  // namespace vse::renderer
