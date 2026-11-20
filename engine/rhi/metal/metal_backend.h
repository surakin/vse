#pragma once

//! Registering the Metal backend with the backend registry (`rhi/backend_registry.h`).

namespace vse::rhi
{

/// Registers the Metal backend (`Backend::Metal`, name `metal`, Metal libraries) with the backend registry. macOS only.
///
/// Called by [[rhi::RegisterCompiledBackends]]; calling it again changes nothing. The backend is available when the
/// system lists a Metal device of the Mac2 family that supports Metal 3. `Auto` picks it on macOS, where it is the only
/// GPU backend (rank 30, from Phase 21 Day 309; from Day 300 it was asked for by name only, as Direct3D 12 was before
/// Phase 13 Day 172).
///
/// - Returns: true if the backend is registered.
/// - Fails: returns false when the registry is full.
/// - Thread-safety: thread-safe.
bool RegisterMetalBackend();

}  // namespace vse::rhi
