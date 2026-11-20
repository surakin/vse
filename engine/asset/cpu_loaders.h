#pragma once

//! The loaders of the resource types that need no GPU, and the traits that tie their C++ types to resource types. Phase
//! 22 Day 328. Day 329 registers GPU-backed loaders for meshes, textures and materials over these.

#include "asset/audio_clip.h"
#include "asset/clip.h"
#include "asset/level.h"
#include "asset/material.h"
#include "asset/mesh.h"
#include "asset/mesh_collider.h"
#include "asset/resource_manager.h"
#include "asset/script_blob.h"
#include "asset/skeleton.h"
#include "asset/texture.h"

namespace vse::asset
{

/// A skeleton is loaded as a [[asset::SkeletonView]] over the manager's buffer.
///
/// - Thread-safety: thread-safe, a compile-time constant.
template <> struct ResourceTraits<SkeletonView>
{
    static constexpr ResourceType kType = types::kSkeleton;                          ///< `SKEL`.
    static constexpr u32 kKind = ResourceType::FromChars('v', 'S', 'K', 'L').Tag();  ///< The code of the C++ type.
};
/// A clip is loaded as a [[asset::ClipView]] over the manager's buffer, bound to its skeleton.
///
/// - Thread-safety: thread-safe, a compile-time constant.
template <> struct ResourceTraits<ClipView>
{
    static constexpr ResourceType kType = types::kAnimation;                         ///< `ANIM`.
    static constexpr u32 kKind = ResourceType::FromChars('v', 'A', 'N', 'M').Tag();  ///< The code of the C++ type.
};
/// A mesh is loaded as a [[asset::MeshView]] over the manager's buffer, checked against its skeleton.
///
/// - Thread-safety: thread-safe, a compile-time constant.
template <> struct ResourceTraits<MeshView>
{
    static constexpr ResourceType kType = types::kMesh;                              ///< `MESH`.
    static constexpr u32 kKind = ResourceType::FromChars('v', 'M', 'S', 'H').Tag();  ///< The code of the C++ type.
};
/// A texture is loaded as a [[asset::TextureView]] over the manager's buffer.
///
/// - Thread-safety: thread-safe, a compile-time constant.
template <> struct ResourceTraits<TextureView>
{
    static constexpr ResourceType kType = types::kTexture;                           ///< `TEXR`.
    static constexpr u32 kKind = ResourceType::FromChars('v', 'T', 'E', 'X').Tag();  ///< The code of the C++ type.
};
/// A material is loaded as its CPU description.
///
/// - Thread-safety: thread-safe, a compile-time constant.
template <> struct ResourceTraits<MaterialAssetData>
{
    static constexpr ResourceType kType = types::kMaterial;                          ///< `MATL`.
    static constexpr u32 kKind = ResourceType::FromChars('d', 'M', 'A', 'T').Tag();  ///< The code of the C++ type.
};
/// An audio clip is loaded as an [[asset::AudioClipView]] over the manager's buffer.
///
/// - Thread-safety: thread-safe, a compile-time constant.
template <> struct ResourceTraits<AudioClipView>
{
    static constexpr ResourceType kType = types::kAudioClip;                         ///< `ACLP`.
    static constexpr u32 kKind = ResourceType::FromChars('v', 'A', 'C', 'L').Tag();  ///< The code of the C++ type.
};
/// A script is loaded as a [[asset::ScriptView]] over the manager's buffer, with the scripts it requires loaded first.
///
/// - Thread-safety: thread-safe, a compile-time constant.
template <> struct ResourceTraits<ScriptView>
{
    static constexpr ResourceType kType = types::kScript;                            ///< `SCRP`.
    static constexpr u32 kKind = ResourceType::FromChars('v', 'S', 'C', 'R').Tag();  ///< The code of the C++ type.
};
/// A mesh collider is loaded as a [[asset::MeshColliderView]] over the manager's buffer (glTF colliders Day 2).
///
/// - Thread-safety: thread-safe, a compile-time constant.
template <> struct ResourceTraits<MeshColliderView>
{
    static constexpr ResourceType kType = types::kMeshCollider;                      ///< `MCOL`.
    static constexpr u32 kKind = ResourceType::FromChars('v', 'M', 'C', 'L').Tag();  ///< The code of the C++ type.
};
/// A level is loaded as its summary (counts and the resources it depends on); spawning it reads the records.
///
/// - Thread-safety: thread-safe, a compile-time constant.
template <> struct ResourceTraits<LevelSummary>
{
    static constexpr ResourceType kType = types::kLevel;                             ///< `LEVL`.
    static constexpr u32 kKind = ResourceType::FromChars('s', 'L', 'E', 'V').Tag();  ///< The code of the C++ type.
};

/// Registers the loaders of audio clips, scripts, skeletons, clips, meshes, textures, materials, mesh colliders and
/// levels with `manager`. A skinned mesh is published only when its skeleton is loaded and its recorded hash and joint
/// count match, and a clip only when its skeleton is the one it was made for (`DependencyTypeMismatch` otherwise).
///
/// - Param manager: the manager.
/// - Ownership: borrows `manager` for the call only; nothing is retained.
void RegisterCpuLoaders(ResourceManager* manager);

/// Registers only the skeleton loader with `manager`: a manager that makes GPU meshes needs it, since a skinned mesh
/// names its skeleton as a dependency that has to load first.
///
/// - Param manager: the manager.
/// - Ownership: borrows `manager` for the call only; nothing is retained.
void RegisterSkeletonLoader(ResourceManager* manager);

}  // namespace vse::asset
