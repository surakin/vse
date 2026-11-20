#pragma once

//! Registering the Direct3D 12 backend with the backend registry (`rhi/backend_registry.h`).

namespace vse::rhi
{

/// Registers the Direct3D 12 backend (`Backend::Dx12`, name `dx12`, DXIL) with the backend registry. Windows only.
///
/// Called by [[rhi::RegisterCompiledBackends]]; calling it again changes nothing. The backend is available when DXGI
/// lists an adapter that can make a device at feature level 12_0. Until the backend passes the Phase 4 and Phase 5
/// suites (Phase 13 Day 172), `Auto` never picks it (rank 0, like the null backend): it is used only when asked for by
/// name.
///
/// - Returns: true if the backend is registered.
/// - Fails: returns false when the registry is full.
/// - Thread-safety: thread-safe.
bool RegisterDx12Backend();

}  // namespace vse::rhi
