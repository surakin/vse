#pragma once

#include "rhi/format.h"
#include "rhi/limits.h"
#include "types.h"

//! The device limits the layers above the RHI depend on.

namespace engine_placeholder::rhi {

/// Limits and optional features of one device, filled in by its backend.
///
/// Default values are the minimums the RHI guarantees on every backend, so code written against the
/// defaults works on any device and on the null backend.
///
/// Thread-safety: thread-compatible plain data; a device returns it by const reference and never changes it.
struct DeviceCaps {
    /// Largest width or height of a 2D texture, in texels.
    u32 max_texture_dimension_2d = 4096;
    /// Largest width, height or depth of a 3D texture, in texels.
    u32 max_texture_dimension_3d = 256;
    /// Largest number of array layers in a texture.
    u32 max_texture_array_layers = 256;
    /// Largest number of bind groups a pipeline can use.
    u32 max_bind_groups = kMaxBindGroups;
    /// Largest number of color attachments in one rendering pass.
    u32 max_color_attachments = 4;
    /// Largest push-constant range in bytes.
    u32 max_push_constant_bytes = kMaxPushConstantBytes;
    /// Required alignment in bytes of a uniform buffer binding offset.
    u32 min_uniform_buffer_offset_alignment = 256;
    /// Required alignment in bytes of a storage buffer binding offset.
    u32 min_storage_buffer_offset_alignment = 256;
    /// Largest sampler anisotropy.
    f32 max_sampler_anisotropy = 1.0f;
    /// Largest number of workgroups per compute dispatch dimension.
    u32 max_compute_work_group_count[3] = {65535, 65535, 65535};
    /// The depth format the device supports for depth attachments.
    Format depth_format = Format::D32_Float;
    /// Nanoseconds per GPU timestamp tick; 0 when timestamps are unsupported.
    f32 timestamp_period_ns = 0.0f;
    /// Whether bindless (descriptor-indexed) resources are supported; reserved, always false in Phase 3.
    bool supports_bindless = false;
};

} // namespace engine_placeholder::rhi
