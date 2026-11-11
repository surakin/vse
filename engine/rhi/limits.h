#pragma once

#include "types.h"

//! RHI-wide size ceilings shared by the descriptors and the default device limits.
//!
//! These are the values every backend is required to support. A device may support more, never less;
//! the descriptors validate against these, and the device validates against its own `DeviceCaps`.

namespace engine_placeholder::rhi
{

/// Largest number of bindings in one bind group layout.
inline constexpr u32 kMaxBindingsPerLayout = 32;
/// Largest array size of one binding (for sampled textures and samplers).
inline constexpr u32 kMaxBindingArraySize = 256;
/// Largest number of bind groups one pipeline may use (the RHI-wide ceiling; devices may allow fewer).
inline constexpr u32 kMaxBindGroups = 4;
/// Largest number of color attachments one pipeline may write (the RHI-wide ceiling).
inline constexpr u32 kMaxColorAttachments = 8;
/// Largest number of vertex buffer bindings.
inline constexpr u32 kMaxVertexBindings = 8;
/// Largest number of vertex attributes.
inline constexpr u32 kMaxVertexAttributes = 16;
/// Largest push-constant range in bytes that any device is required to support.
inline constexpr u32 kMaxPushConstantBytes = 128;

}  // namespace engine_placeholder::rhi
