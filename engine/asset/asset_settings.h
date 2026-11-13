#pragma once

//! The asset settings (Phase 22 Day 315): [[asset::RegisterAssetSettings]] declares the byte budgets of the load buffer
//! pool and of the memory backend; the two readers return them.

#include "settings/registry.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// Declares `assets.buffer_pool_bytes` (the bound of [[asset::LoadBufferPool]]) and `assets.memory_backend_bytes` (the
/// bound of [[asset::HostMemoryBackend]]). A key that is already declared is left as it is.
///
/// - Thread-safety: thread-compatible; call it while the registry is being set up.
/// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
void RegisterAssetSettings(settings::Registry* registry);

/// The value of `assets.buffer_pool_bytes`.
///
/// - Aborts: the setting was not declared.
/// - Thread-safety: thread-safe.
u64 BufferPoolBytesFromSettings(const settings::Registry& registry);

/// The value of `assets.memory_backend_bytes`.
///
/// - Aborts: the setting was not declared.
/// - Thread-safety: thread-safe.
u64 MemoryBackendBytesFromSettings(const settings::Registry& registry);

}  // namespace engine_placeholder::asset
