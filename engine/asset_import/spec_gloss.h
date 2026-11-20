#pragma once

//! Converting `KHR_materials_pbrSpecularGlossiness` materials to the metallic-roughness model.
//!
//! The engine's material templates are metallic-roughness only, so the importer converts a specular-glossiness
//! material when it reads it (maintenance 2026-11-20). The conversion is the one the Khronos sample tools use: the
//! metallic value is solved from the perceived brightness of the diffuse and specular colours, the base colour blends
//! the colour each of them implies by the square of the metallic value, and the roughness is one minus the glossiness.
//! Colours are linear here; the textures' RGB is sRGB and their alpha linear, as the extension says.

#include "asset_import/image_decode.h"
#include "containers/array.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// One specular-glossiness sample, linear.
///
/// - Thread-safety: thread-compatible plain data.
struct SpecGlossSample
{
    Array<f32, 4> diffuse = {{1.0f, 1.0f, 1.0f, 1.0f}};  ///< The diffuse colour and the opacity.
    Array<f32, 3> specular = {{1.0f, 1.0f, 1.0f}};       ///< The specular colour.
    f32 glossiness = 1.0f;                               ///< The glossiness, 0 to 1.
};

/// One metallic-roughness sample, linear.
///
/// - Thread-safety: thread-compatible plain data.
struct MetalRoughSample
{
    Array<f32, 4> base_color = {{1.0f, 1.0f, 1.0f, 1.0f}};  ///< The base colour (each channel 0 to 1) and the opacity.
    f32 metallic = 0.0f;                                    ///< The metallic value, 0 to 1.
    f32 roughness = 0.0f;                                   ///< The roughness, 0 to 1.
};

/// Converts one specular-glossiness sample.
///
/// A specular colour darker than the dielectric 4% gives a metallic value of 0, whatever the diffuse colour.
///
/// - Param in: the sample.
/// - Returns: the metallic-roughness sample; the opacity is the diffuse one.
/// - Fails: never.
/// - Thread-safety: thread-safe.
MetalRoughSample SpecGlossToMetalRough(const SpecGlossSample& in);

/// Tells whether a specular colour is below the dielectric 4% (by perceived brightness).
///
/// Every sample with such a specular colour converts to a metallic value of 0 and a base colour linear in the diffuse
/// (the diffuse times a scale, clamped at 1), whatever its diffuse colour.
///
/// - Param specular: the linear specular colour.
/// - Fails: never.
/// - Thread-safety: thread-safe.
bool SpecularIsDielectric(const Array<f32, 3>& specular);

/// The inputs of a texture conversion: the two images (either may be absent) and the material's factors.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: borrows the images.
struct SpecGlossImages
{
    const DecodedImage* diffuse = nullptr;                      ///< `diffuseTexture`'s pixels, or none.
    const DecodedImage* specular_glossiness = nullptr;          ///< `specularGlossinessTexture`'s, or none.
    Array<f32, 4> diffuse_factor = {{1.0f, 1.0f, 1.0f, 1.0f}};  ///< `diffuseFactor`.
    Array<f32, 3> specular_factor = {{1.0f, 1.0f, 1.0f}};       ///< `specularFactor`.
    f32 glossiness_factor = 1.0f;                               ///< `glossinessFactor`.
};

/// Converts a specular-glossiness material's textures to a base-colour and a metallic-roughness image.
///
/// The images' RGB is sRGB and their alpha linear. Every output texel is the conversion of the factors times the two
/// images' samples at its centre. The outputs are as large as the larger image on each side; a smaller image is sampled
/// bilinearly (clamped at the edges), in linear space.
///
/// - Param in: the images and factors; at least one image is present.
/// - Param max_pixels: the most pixels an output may have.
/// - Param base_color: if not null, replaced by the base colour (RGB sRGB, A the opacity; `has_alpha` when an opacity
/// is below 255).
/// - Param metal_rough: if not null, replaced by the glTF packing: G the roughness, B the metallic value, R and A 255.
/// - Fails: `TooLarge` (the output would be above `max_pixels`), `LimitExceeded` (memory).
/// - Aborts: neither image is present.
/// - Thread-safety: thread-safe for separate calls.
/// - Ownership: borrows the images for the call only.
Status ConvertSpecGlossImages(const SpecGlossImages& in, u64 max_pixels, DecodedImage* base_color,
                              DecodedImage* metal_rough);

}  // namespace engine_placeholder::asset
