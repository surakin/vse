#pragma once

//! The one call that registers every RHI backend this build compiled: library `rhi_backends`.

namespace engine_placeholder::rhi {

/// Registers every backend compiled into this build with the backend registry: the null backend and Vulkan everywhere,
/// Direct3D 12 on Windows (Phase 13 Day 164).
///
/// A program calls this once at startup, before [[rhi::CreateDevice]], [[rhi::EnumerateAdapters]] or a
/// [[rhi::DeviceHost]]; calling it again changes nothing. Registration is explicit, never a global constructor, so a
/// program that does not link a backend cannot pick it by accident and one that forgets the call fails with a clear error.
///
/// - Returns: true if every backend registered.
/// - Fails: returns false (each failure logged) when one did not register.
/// - Thread-safety: thread-safe.
bool RegisterCompiledBackends();

} // namespace engine_placeholder::rhi
