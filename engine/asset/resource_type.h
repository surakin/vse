#pragma once

//! [[asset::ResourceType]] (a four-character tag) and [[asset::TypeRegistry]] (the known types and the one current
//! version of each, the single source of truth the minters, the packer, `verify` and the manager share).

#include "asset/status.h"
#include "containers/vector.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// A resource type: a 32-bit tag written as four characters (`MESH`, `TEXR`, `MATL`, `LEVL`).
///
/// - Thread-safety: immutable value.
class ResourceType
{
public:
    /// Creates the null type (tag 0), which is no type.
    constexpr ResourceType() = default;

    /// Returns the type with the tag `tag`.
    /// - Fails: never.
    static constexpr ResourceType FromTag(u32 tag)
    {
        ResourceType t;
        t.tag_ = tag;
        return t;
    }
    /// Returns the type spelled by four characters, the first in the lowest byte.
    /// - Fails: never.
    static constexpr ResourceType FromChars(char a, char b, char c, char d)
    {
        return FromTag(static_cast<u32>(static_cast<u8>(a)) | (static_cast<u32>(static_cast<u8>(b)) << 8) |
                       (static_cast<u32>(static_cast<u8>(c)) << 16) | (static_cast<u32>(static_cast<u8>(d)) << 24));
    }

    /// The tag.
    /// - Fails: never.
    constexpr u32 Tag() const { return tag_; }
    /// Returns true for the null type.
    /// - Fails: never.
    constexpr bool IsNone() const { return tag_ == 0; }

    /// Writes the four characters and a terminating zero to `out` (a non-printable byte becomes `?`).
    /// - Fails: never.
    void Spell(char (&out)[5]) const;

    /// Compares tags.
    /// - Fails: never.
    friend constexpr bool operator==(ResourceType a, ResourceType b) { return a.tag_ == b.tag_; }
    /// Compares tags.
    /// - Fails: never.
    friend constexpr bool operator!=(ResourceType a, ResourceType b) { return a.tag_ != b.tag_; }

private:
    u32 tag_ = 0;
};

/// The types of this phase.
namespace types
{
/// A mesh (Day 319).
inline constexpr ResourceType kMesh = ResourceType::FromChars('M', 'E', 'S', 'H');
/// A texture (Day 323).
inline constexpr ResourceType kTexture = ResourceType::FromChars('T', 'E', 'X', 'R');
/// A material (Day 326).
inline constexpr ResourceType kMaterial = ResourceType::FromChars('M', 'A', 'T', 'L');
/// A level (Day 327).
inline constexpr ResourceType kLevel = ResourceType::FromChars('L', 'E', 'V', 'L');
/// An animation clip: per-joint keyframe tracks and events for one skeleton (Day 320).
inline constexpr ResourceType kAnimation = ResourceType::FromChars('A', 'N', 'I', 'M');
/// A skeleton: the joint hierarchy, bind pose and inverse bind matrices (Day 318).
inline constexpr ResourceType kSkeleton = ResourceType::FromChars('S', 'K', 'E', 'L');
/// A convex hull for physics (Phase 25 Day 393).
inline constexpr ResourceType kHull = ResourceType::FromChars('H', 'U', 'L', 'L');
/// A triangle mesh for static physics colliders (Phase 25 Day 394).
inline constexpr ResourceType kMeshCollider = ResourceType::FromChars('M', 'C', 'O', 'L');
/// A decoded audio clip: interleaved samples with a rate, a layout and loop points (Phase 26 Day 418).
inline constexpr ResourceType kAudioClip = ResourceType::FromChars('A', 'C', 'L', 'P');
/// A script: its source text, the entry points the minter found and its dependencies (Phase 27 Day 462).
inline constexpr ResourceType kScript = ResourceType::FromChars('S', 'C', 'R', 'P');
/// The id <-> path table (a blob that is not itself a resource: its header id may be null).
inline constexpr ResourceType kNames = ResourceType::FromChars('N', 'A', 'M', 'E');
}  // namespace types

/// One registered type.
///
/// - Thread-safety: immutable value.
struct TypeInfo
{
    ResourceType type;        ///< The tag.
    u32 current_version = 0;  ///< The one version the runtime reads (never 0 for a registered type).
    const char* name = "";    ///< A static name for messages.
};

/// The known resource types and their current versions. A fresh registry holds the built-in types of this phase at
/// version 1 (each format day raises its own type's version when its format changes, and a golden old blob keeps the
/// refusal tested).
///
/// - Thread-safety: thread-compatible; build it once, then read it from many threads.
/// - Ownership: owns its table.
class TypeRegistry
{
public:
    /// Creates a registry holding the built-in types.
    TypeRegistry();

    /// Registers `type` at `version`.
    ///
    /// - Param name: a string literal; never null.
    /// - Fails: `Invalid` for the null type or version 0; `DuplicateId` when the type is registered already.
    Status Register(ResourceType type, u32 version, const char* name);

    /// Looks `type` up.
    ///
    /// - Param out: receives the entry when found; may be null.
    /// - Ownership: `out` is written, never retained; the entry's name is a static string.
    /// - Returns: true when `type` is registered.
    /// - Fails: never.
    bool Find(ResourceType type, TypeInfo* out) const;

    /// Returns the number of registered types.
    /// - Fails: never.
    usize Size() const { return types_.Size(); }

    /// Returns the `index`th registered type (in registration order, the same on every run).
    ///
    /// - Aborts: `index` is not below `Size()`.
    const TypeInfo& At(usize index) const { return types_[index]; }

private:
    Vector<TypeInfo> types_;
};

}  // namespace engine_placeholder::asset
