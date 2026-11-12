#pragma once

//! Registering the Metal backend with the backend registry (`rhi/backend_registry.h`).

namespace engine_placeholder::rhi
{

/// Registers the Metal backend (`Backend::Metal`, name `metal`, Metal libraries) with the backend registry. macOS only.
///
/// Called by [[rhi::RegisterCompiledBackends]]; calling it again changes nothing. The backend is available when the
/// system lists a Metal device of the Mac2 family that supports Metal 3. Until the backend passes the Phase 4 and Phase
/// 5 suites (Phase 26 Day 325), `Auto` never picks it (rank 0, as Direct3D 12 before Phase 13 Day 172): it is used only
/// when asked for by name, and the `backend` setting's `auto` stays the null backend on macOS
/// ([[renderer::BackendForSetting]]).
///
/// - Returns: true if the backend is registered.
/// - Fails: returns false when the registry is full.
/// - Thread-safety: thread-safe.
bool RegisterMetalBackend();

}  // namespace engine_placeholder::rhi
