#pragma once

//! Registering the Metal backend with the backend registry (`rhi/backend_registry.h`).

namespace vse::rhi
{

/// Registers the Metal backend (`Backend::Metal`, name `metal`, Metal libraries) with the backend registry. macOS only.
///
/// Called by [[rhi::RegisterCompiledBackends]]; calling it again changes nothing. The backend is available when the
/// system lists a Metal device of the Mac2 family that supports Metal 3. `Auto` picks it on macOS, where it is the only
/// GPU backend (rank 30).
///
/// - Returns: true if the backend is registered.
/// - Fails: returns false when the registry is full.
/// - Thread-safety: thread-safe.
bool RegisterMetalBackend();

}  // namespace vse::rhi
