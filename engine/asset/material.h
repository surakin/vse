#pragma once

//! The material blob (type `MATL`, version 1): a template name, shader permutation options, the parameter block in the
//! template's uniform layout, texture bindings by resource id, sampler descriptions and render-state overrides.
//! Built by [[asset::BuildMaterialBlob]] and read by [[asset::LoadMaterialBlob]].
//!
//! Sections (ids from [[asset::kFirstUserSection]]): 16 the header (96 bytes: flags, counts, the state overrides and
//! the 48-byte template name), 17 the parameter block (the bytes copied into the material's uniform buffer, laid out as
//! the template's block, so loading copies one block), 18 the parameter table (name, type, offset and size of each
//! value the source set; 64 bytes each), 19 the texture bindings (slot name and resource id; 64 bytes each), 20 the
//! samplers (64 bytes each), 21 the permutation options (48-byte names). Every texture id is also a dependency of the
//! blob (type `TEXR`, role = the binding's index), so the loader follows the references without reading the sections.

#include "asset/blob.h"
#include "asset/resource_id.h"
#include "asset/status.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "types.h"

namespace vse::asset
{

/// Bytes of a name (template, parameter, slot, option) including its terminating zero.
inline constexpr u32 kMaterialNameBytes = 48;
/// Most texture bindings of a material (the renderer's `kMaxMaterialTextures`).
inline constexpr u32 kMaterialMaxTextures = 8;
/// Most sampler descriptions of a material (the renderer's `kMaxMaterialSamplers`).
inline constexpr u32 kMaterialMaxSamplers = 4;
/// Largest parameter block in bytes (the renderer's `kMaxMaterialParamBytes`).
inline constexpr u32 kMaterialMaxParamBytes = 1024;
/// Most parameter values a material records (the shader reflection's member limit).
inline constexpr u32 kMaterialMaxParams = 16;
/// Most permutation options a material names.
inline constexpr u32 kMaterialMaxOptions = 16;

/// A name in a fixed field: at most 47 characters and a zero.
///
/// - Thread-safety: thread-compatible plain data.
struct MaterialName
{
    char text[kMaterialNameBytes] = {};  ///< The characters, zero-terminated.
};

/// Flag bits of a material.
enum MaterialFlags : u32
{
    kMaterialSkinnable = 1u << 0,  ///< The template's vertex shader has the `SKINNED` permutation: for skinned meshes.
};

/// The type of a recorded parameter value.
enum class MaterialParamType : u8
{
    Float = 0,   ///< One float.
    Int = 1,     ///< One signed integer.
    UInt = 2,    ///< One unsigned integer.
    Vec2 = 3,    ///< Two floats.
    Vec3 = 4,    ///< Three floats.
    Vec4 = 5,    ///< Four floats.
    Mat4 = 6,    ///< Sixteen floats, column-major as the block holds them.
    Color3 = 7,  ///< Three floats the source wrote as a color (the tag only tells tools to show a color picker).
    Color4 = 8,  ///< Four floats the source wrote as a color.
};

/// One parameter value the source set; its bytes are in the parameter block at `offset`.
///
/// - Thread-safety: thread-compatible plain data.
struct MaterialParam
{
    MaterialName name;                                  ///< The block member's name.
    MaterialParamType type = MaterialParamType::Float;  ///< The type.
    u32 offset = 0;                                     ///< Byte offset in the block.
    u32 size = 0;                                       ///< Bytes of the value.
};

/// A texture slot bound to a texture resource.
///
/// - Thread-safety: thread-compatible plain data.
struct MaterialTextureBinding
{
    MaterialName slot;   ///< The shader's texture variable name.
    ResourceId texture;  ///< The texture (never null).
};

/// Texture filtering of a sampler description.
enum class MaterialFilter : u8
{
    Nearest = 0,  ///< Point sampling.
    Linear = 1,   ///< Linear interpolation.
};

/// Texture coordinate addressing of a sampler description.
enum class MaterialAddress : u8
{
    Repeat = 0,        ///< The coordinate wraps.
    ClampToEdge = 1,   ///< The coordinate is clamped.
    MirrorRepeat = 2,  ///< The coordinate mirrors at each integer.
};

/// A sampler slot's description.
///
/// - Thread-safety: thread-compatible plain data.
struct MaterialSampler
{
    MaterialName slot;                                    ///< The shader's sampler variable name.
    MaterialFilter min_filter = MaterialFilter::Linear;   ///< Minification.
    MaterialFilter mag_filter = MaterialFilter::Linear;   ///< Magnification.
    MaterialFilter mip_filter = MaterialFilter::Linear;   ///< Between mip levels.
    MaterialAddress address_u = MaterialAddress::Repeat;  ///< Horizontal addressing.
    MaterialAddress address_v = MaterialAddress::Repeat;  ///< Vertical addressing.
    u8 max_anisotropy = 1;                                ///< 1 to 16.
};

/// Which render-state fields a material overrides, and their values (the fields of the renderer's
/// `MaterialRenderState`; the numbers are `rhi::CullMode` and `rhi::CompareOp`).
///
/// - Thread-safety: thread-compatible plain data.
struct MaterialStateOverride
{
    bool set_cull = false;           ///< `cull` overrides the template's.
    bool set_depth_test = false;     ///< `depth_test` overrides.
    bool set_depth_write = false;    ///< `depth_write` overrides.
    bool set_depth_compare = false;  ///< `depth_compare` overrides.
    bool set_alpha_blend = false;    ///< `alpha_blend` overrides.
    u8 cull = 2;                     ///< `rhi::CullMode`: 0 none, 1 front, 2 back.
    bool depth_test = true;          ///< Whether the depth test runs.
    bool depth_write = true;         ///< Whether depth is written.
    u8 depth_compare = 1;            ///< `rhi::CompareOp` (0 to 7).
    bool alpha_blend = false;        ///< Source-alpha blending.
};

/// The CPU description of a material: what the minter builds and the loader returns. The resource manager
/// turns it into a `renderer::MaterialId`.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its vectors.
struct MaterialAssetData
{
    MaterialName template_name;    ///< The built-in template (`gbuffer` and the like).
    u32 flags = 0;                 ///< [[asset::MaterialFlags]].
    MaterialStateOverride state;   ///< The render-state overrides.
    Vector<MaterialName> options;  ///< The fragment permutation options the material enables.
    Vector<byte> block;            ///< The parameter block (the template's uniform layout), up to 1024 bytes.
    Vector<MaterialParam> params;  ///< The values the source set, with their place in `block`.
    Vector<MaterialTextureBinding> textures;  ///< The texture slots, in a stable order.
    Vector<MaterialSampler> samplers;         ///< The sampler slots.
};

/// Copies `text` into a name field.
///
/// - Param text: at most 47 characters (no zero inside).
/// - Param out: receives the name.
/// - Returns: false when it does not fit; `*out` is then empty.
/// - Fails: returns false as described.
/// - Ownership: borrows `text` for the call; `*out` is written.
bool MakeMaterialName(const char* text, usize length, MaterialName* out);

/// Builds a material blob for `id`: validates `data`, writes the sections and declares each texture as a dependency.
///
/// - Param types: the registry (the texture type's version goes into the dependency records).
/// - Param id: the material's id (never null).
/// - Param data: the description.
/// - Param out: replaced by the blob.
/// - Param target: the target tag recorded in the header.
/// - Fails: `Invalid` for a null id, an empty or unterminated name, a repeated name, counts or sizes above the limits,
/// a
///   parameter outside the block, overlapping parameters, a null or repeated texture id, a repeated slot, a sampler
///   outside its ranges or a state value outside its enum; `LimitExceeded` when memory runs out.
/// - Ownership: borrows `data` for the call; `*out` is replaced.
Status BuildMaterialBlob(const TypeRegistry& types, ResourceId id, const MaterialAssetData& data, Vector<byte>* out,
                         u32 target = 0);

/// Reads a material blob into a description. Everything is validated: the sections' sizes against the header, every
/// name, the parameters against the block, the enums and ranges, and the dependency list against the texture bindings.
///
/// - Param bytes: the blob.
/// - Param types: the registry.
/// - Param out: replaced by the description.
/// - Param id_out: receives the blob's own id when not null.
/// - Fails: `Corrupt` for any inconsistency; `UnsupportedVersion` (the message names `asset_mint remint`) for another
///   container or type version; `TypeMismatch` for a blob of another type; `LimitExceeded` when memory runs out.
/// - Ownership: borrows `bytes` for the call; `*out` is replaced.
Status LoadMaterialBlob(ByteSpan bytes, const TypeRegistry& types, MaterialAssetData* out,
                        ResourceId* id_out = nullptr);

}  // namespace vse::asset
