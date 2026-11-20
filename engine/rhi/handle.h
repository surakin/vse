#pragma once

#include "types.h"

//! Opaque, generation-checked handles to device-owned GPU resources.
//!
//! A handle is a plain 8-byte value. The device validates it on every use, so a handle to a
//! destroyed resource is detected rather than dereferenced.

namespace vse::rhi
{

/// A generation-checked reference to one device-owned resource of the kind named by `Tag`.
///
/// The all-zero value is the canonical invalid handle; test validity with `IsValid()` (a zero generation),
/// not by comparing with a default-constructed handle. Live handles always have a non-zero generation, which the
/// device bumps when the slot is destroyed, so a stale copy compares unequal to the current handle for
/// that slot and is rejected by the device. Handles from different devices must not be mixed.
///
/// - Thread-safety: thread-safe. An immutable-style plain value; copying and comparing need no synchronization.
/// - Ownership: a handle does not own the resource; the resource lives until the device's `Destroy*` call
///   and the GPU has finished with it.
template <typename Tag> struct Handle
{
    /// Slot index inside the device's pool for this resource kind.
    u32 index = 0;
    /// Generation of the slot when the handle was created; 0 means invalid.
    u32 generation = 0;

    /// Returns whether this is not the invalid (all-zero) handle.
    ///
    /// - Returns: true when `generation` is non-zero; a valid-looking handle can still be stale.
    constexpr bool IsValid() const { return generation != 0; }

    /// Returns whether both handles have the same index and generation.
    constexpr bool operator==(const Handle& other) const
    {
        return index == other.index && generation == other.generation;
    }
    /// Returns whether the handles differ in index or generation.
    constexpr bool operator!=(const Handle& other) const { return !(*this == other); }
};

/// Tag type distinguishing [[rhi::BufferHandle]] from the other handle kinds.
///
/// - Thread-safety: immutable. An empty type.
struct BufferTag
{
};
/// Tag type distinguishing [[rhi::TextureHandle]] from the other handle kinds.
///
/// - Thread-safety: immutable. An empty type.
struct TextureTag
{
};
/// Tag type distinguishing [[rhi::SamplerHandle]] from the other handle kinds.
///
/// - Thread-safety: immutable. An empty type.
struct SamplerTag
{
};
/// Tag type distinguishing [[rhi::ShaderHandle]] from the other handle kinds.
///
/// - Thread-safety: immutable. An empty type.
struct ShaderTag
{
};
/// Tag type distinguishing [[rhi::PipelineHandle]] from the other handle kinds.
///
/// - Thread-safety: immutable. An empty type.
struct PipelineTag
{
};
/// Tag type distinguishing [[rhi::BindGroupLayoutHandle]] from the other handle kinds.
///
/// - Thread-safety: immutable. An empty type.
struct BindGroupLayoutTag
{
};
/// Tag type distinguishing [[rhi::BindGroupHandle]] from the other handle kinds.
///
/// - Thread-safety: immutable. An empty type.
struct BindGroupTag
{
};

/// Handle to a GPU buffer.
using BufferHandle = Handle<BufferTag>;
/// Handle to a GPU texture (image).
using TextureHandle = Handle<TextureTag>;
/// Handle to a sampler.
using SamplerHandle = Handle<SamplerTag>;
/// Handle to a shader module.
using ShaderHandle = Handle<ShaderTag>;
/// Handle to a graphics or compute pipeline.
using PipelineHandle = Handle<PipelineTag>;
/// Handle to a bind group layout.
using BindGroupLayoutHandle = Handle<BindGroupLayoutTag>;
/// Handle to a bind group.
using BindGroupHandle = Handle<BindGroupTag>;

}  // namespace vse::rhi
