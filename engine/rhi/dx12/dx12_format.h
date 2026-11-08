#pragma once

#include "rhi/dx12/d3d12_api.h"
#include "rhi/enums.h"
#include "rhi/format.h"
#include "types.h"

//! The RHI's formats as `DXGI_FORMAT`s (Phase 13 Day 165).

namespace engine_placeholder::rhi::dx12 {

/// Returns the typed `DXGI_FORMAT` of a format, the one views and copies of a color format use.
///
/// Param format: an RHI format.
/// Returns: the DXGI format; `DXGI_FORMAT_UNKNOWN` for `Undefined`.
/// Fails: never.
/// Thread-safety: thread-safe.
DXGI_FORMAT ToDxgi(Format format);

/// Returns the format a texture's resource is created with: the typed format, except a depth format that is also sampled, which is
/// created typeless (`R32_TYPELESS` for `D32_Float`, `R24G8_TYPELESS`, `R32G8X24_TYPELESS`) so that a depth view and a shader view of
/// different types can both exist (the Phase 4 portability review, "typeless formats for sampled depth").
///
/// Param format: the texture's format.
/// Param usage: its usage.
/// Returns: the resource format.
/// Fails: never.
/// Thread-safety: thread-safe.
DXGI_FORMAT ToDxgiResource(Format format, TextureUsage usage);

/// Returns the format of a shader-resource view: the typed format, or for a depth format the readable color form (`R32_FLOAT`,
/// `R24_UNORM_X8_TYPELESS`, `R32_FLOAT_X8X24_TYPELESS`).
///
/// Param format: the texture's format.
/// Returns: the view format.
/// Fails: never.
/// Thread-safety: thread-safe.
DXGI_FORMAT ToDxgiShaderView(Format format);

/// Returns the format of a depth-stencil view: the typed depth format.
///
/// Param format: a depth format.
/// Returns: the view format; `DXGI_FORMAT_UNKNOWN` for a color format.
/// Fails: `DXGI_FORMAT_UNKNOWN`, as described.
/// Thread-safety: thread-safe.
DXGI_FORMAT ToDxgiDepthView(Format format);

/// Returns the `DXGI_FORMAT` of a vertex attribute format.
///
/// Param format: the vertex format.
/// Returns: the DXGI format.
/// Fails: never.
/// Thread-safety: thread-safe.
DXGI_FORMAT ToDxgiVertex(VertexFormat format);

/// Returns the resource state an RHI state maps to; `Undefined` and `Present` are `D3D12_RESOURCE_STATE_COMMON` (the same value as
/// `PRESENT`).
///
/// Param state: the RHI state.
/// Returns: the D3D12 state.
/// Fails: never.
/// Thread-safety: thread-safe.
D3D12_RESOURCE_STATES ToD3D12State(ResourceState state);

} // namespace engine_placeholder::rhi::dx12
