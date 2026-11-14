#pragma once

//! The text source form of a material (`.mat.src`) and the material minter. Phase 22 Day 326. An offline tool's code:
//! never linked into the engine; its tests compile it directly.
//!
//! Line-oriented text; `#` starts a comment; fields are separated by spaces or tabs:
//!
//!     id 00000000000000aa
//!     template gbuffer
//!     option NORMAL_MAP
//!     param base_color color 0.8 0.2 0.1 1
//!     param roughness float 0.4
//!     texture albedo_tex textures/brick_albedo
//!     sampler linear_sampler filter linear address repeat anisotropy 8
//!     state cull none
//!
//! `id` comes first and `template` second. `template` names a built-in template (`gbuffer`, `gbuffer_color`,
//! `gbuffer_skinned`, `gbuffer_skinned_color`); each `option` is one of the template's fragment permutation options.
//! `param NAME TYPE VALUES` sets a member of the template's parameter block (TYPE is `float`, `int`, `uint`, `vec2`,
//! `vec3`, `vec4`, `mat4` or `color`, the last with three or four values; the member's own type decides which are
//! accepted); a member the source does not set keeps the template's default (zero when it has none). `texture SLOT
//! PATH` binds a texture slot to the resource at PATH; `sampler SLOT [filter nearest|linear] [address
//! repeat|clamp|mirror] [anisotropy N]` describes a sampler slot (`filter` sets minification, magnification and mip
//! filtering together); `state FIELD VALUE` overrides one render-state field (`cull none|front|back`, `depth_test
//! on|off`, `depth_write on|off`, `depth_compare never|less|equal|lessequal|greater|notequal|greaterequal|always`,
//! `alpha_blend on|off`).

#include "asset/material.h"
#include "asset/resource_id.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// Where and why a material source was refused.
///
/// - Thread-safety: immutable value.
struct MaterialSourceError
{
    u32 line = 0;                   ///< 1-based line number; 0 when the error is about the file as a whole.
    Array<char, 256> message = {};  ///< A sentence (a parameter error lists what the template expects).
};

/// Finds the resources a material source names, so the minter can record their ids and check their metadata.
///
/// - Thread-safety: implementation-defined; the minter calls it from one thread.
class MaterialReferenceResolver
{
public:
    virtual ~MaterialReferenceResolver() = default;

    /// Resolves a texture path.
    ///
    /// - Param path: the path as written in the source.
    /// - Param id: receives the texture's id.
    /// - Param blob: receives the texture blob's bytes (borrowed from the resolver; valid until it is destroyed).
    /// - Fails: `NotFound` when the project has no such resource; any other error aborts the mint.
    virtual Status Resolve(StringView path, ResourceId* id, ByteSpan* blob) const = 0;
};

/// The material minter: source text to blob bytes, deterministic. Parameters are checked against the template's
/// reflected uniform block and written in its layout; a referenced texture must exist and fit the slot's shape and
/// color space; the dependency list is the set of referenced textures.
///
/// - Param text: the source.
/// - Param resolver: finds the textures.
/// - Param types: the registry.
/// - Param out: replaced by the blob.
/// - Param error: receives the line and message of a refusal.
/// - Param target: the target tag recorded in the header.
/// - Fails: `Invalid` with `error` filled in: a missing or repeated header line, an unknown record, a wrong field
///   count, an unknown template, option, parameter, slot, enum value or number, a parameter of the wrong type for its
///   member (the message lists the template's members), a texture that does not exist or whose kind or color space the
///   slot does not accept, and the limits of [[asset::BuildMaterialBlob]]; `LimitExceeded` when memory runs out.
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status MintMaterial(StringView text, const MaterialReferenceResolver& resolver, const TypeRegistry& types,
                    Vector<byte>* out, MaterialSourceError* error, u32 target = 0);

}  // namespace engine_placeholder::asset
