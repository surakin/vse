#pragma once

//! Registering the Vulkan backend with the backend registry (`rhi/backend_registry.h`).

namespace engine_placeholder::rhi
{

/// Registers the Vulkan backend (`Backend::Vulkan`, name `vulkan`, SPIR-V) with the backend registry.
///
/// Called by [[rhi::RegisterCompiledBackends]]; calling it again changes nothing. The backend is available when the
/// Vulkan loader library opens; `Auto` picks it unless a backend with a higher rank is available (Direct3D 12 on
/// Windows, once that backend is complete).
///
/// - Returns: true if the backend is registered.
/// - Fails: returns false when the registry is full.
/// - Thread-safety: thread-safe.
bool RegisterVulkanBackend();

}  // namespace engine_placeholder::rhi
