#pragma once

#include "rhi/adapter.h"
#include "rhi/device.h"
#include "rhi/enums.h"
#include "string_view.h"
#include "types.h"

//! The table of RHI backends a program was built with: each backend library registers itself, and [[rhi::CreateDevice]],
//! [[rhi::EnumerateAdapters]] and [[rhi::DeviceHost]] reach backends only through it.
//!
//! Registration is an explicit call, never a global constructor: a program calls [[rhi::RegisterCompiledBackends]]
//! (`rhi/backends.h`, library `rhi_backends`) once at startup, before it creates a device; test binaries do it from a
//! Catch2 listener. A device asked of a backend that was never registered fails with an error that says so.

namespace engine_placeholder::rhi {

enum class Backend : u8;
struct DeviceCreateInfo;

/// The most backends a program can register.
inline constexpr usize kMaxRegisteredBackends = 8;

/// What a backend library registers.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `name` and the function pointers refer to static data and code in the backend library.
struct BackendDescriptor {
    /// The backend's value; unique in the registry. Never `Backend::Auto`.
    Backend backend{};
    /// The lowercase name used by the `backend` setting and in logs (for example `vulkan`); unique in the registry.
    const char* name = "";
    /// Creates a device; see [[rhi::CreateDevice]]. Called with `info.backend` equal to `backend`.
    DevicePtr (*create)(const DeviceCreateInfo& info) = nullptr;
    /// Lists the adapters; see [[rhi::EnumerateAdapters]].
    bool (*enumerate_adapters)(const DeviceCreateInfo& info, AdapterList* out) = nullptr;
    /// Says whether the backend can run on this machine without creating a device (for example, whether its loader or
    /// system library is present). Writes the reason into `reason` (`reason_size` bytes, terminated) when it cannot.
    /// Null means always available.
    bool (*available)(char* reason, usize reason_size) = nullptr;
    /// The bytecode format the backend's devices take.
    ShaderFormat shader_format = ShaderFormat::Spirv;
    /// How strongly `Backend::Auto` prefers this backend: the available backend with the highest rank wins. 0 means `Auto`
    /// never picks it (the null backend).
    u32 auto_rank = 0;
};

/// Adds a backend to the registry.
///
/// Registering the same descriptor again (same backend, same functions) is accepted and changes nothing, so a program may
/// call [[rhi::RegisterCompiledBackends]] more than once.
///
/// Param descriptor: copied.
/// Returns: true if the backend is registered (now or already).
/// Fails: returns false, logging the reason, when the table is full, the descriptor has no name, no `create` or no
///   `enumerate_adapters`, its backend is `Auto`, or another descriptor already holds its backend value or name.
/// Thread-safety: thread-safe.
bool RegisterBackend(const BackendDescriptor& descriptor);

/// Returns the registered descriptor of a backend.
///
/// Param backend: the backend; `Auto` finds nothing.
/// Returns: the descriptor, or null when the backend is not registered in this program.
/// Fails: null, as described.
/// Ownership: the descriptor lives in the registry until [[rhi::ClearBackendRegistryForTest]].
/// Thread-safety: thread-safe.
const BackendDescriptor* FindBackend(Backend backend);

/// Returns the registered descriptor whose name equals `name` (case-sensitive).
///
/// Param name: the name, for example `vulkan`.
/// Returns: the descriptor, or null.
/// Fails: null when no registered backend has that name.
/// Ownership: as [[rhi::FindBackend]].
/// Thread-safety: thread-safe.
const BackendDescriptor* FindBackendByName(StringView name);

/// Returns how many backends are registered.
///
/// Thread-safety: thread-safe.
usize RegisteredBackendCount();

/// Returns a registered backend, in registration order.
///
/// Param index: below [[rhi::RegisteredBackendCount]].
/// Returns: the descriptor.
/// Aborts: `index` is out of range.
/// Ownership: as [[rhi::FindBackend]].
/// Thread-safety: thread-safe.
const BackendDescriptor& RegisteredBackend(usize index);

/// Says whether a registered backend can run here.
///
/// Param descriptor: a registered backend.
/// Param reason: receives why not, terminated; may be null.
/// Param reason_size: size of `reason` in bytes.
/// Returns: true if its availability probe passes (or it has none).
/// Fails: false with the reason, as described.
/// Ownership: `reason` belongs to the caller; nothing is kept.
/// Thread-safety: thread-safe.
bool BackendAvailable(const BackendDescriptor& descriptor, char* reason, usize reason_size);

/// Picks the backend `Backend::Auto` means: the available registered backend with the highest `auto_rank`; among equal
/// ranks the one registered first.
///
/// Param reason: receives why none could be picked, terminated; may be null.
/// Param reason_size: size of `reason` in bytes.
/// Returns: the descriptor, or null.
/// Fails: null when no registered backend with a rank above 0 is available.
/// Ownership: as [[rhi::FindBackend]].
/// Thread-safety: thread-safe.
const BackendDescriptor* PickAutoBackend(char* reason, usize reason_size);

/// Resolves the backend a request names: `Auto` to the one [[rhi::PickAutoBackend]] picks, any other value to its
/// registered descriptor if it is available.
///
/// Param backend: the requested backend.
/// Param reason: receives why it cannot be used, terminated (not registered in this program, or not available here, with
///   the backend's own reason); may be null.
/// Param reason_size: size of `reason` in bytes.
/// Returns: the descriptor, or null.
/// Fails: null with the reason, as described.
/// Ownership: as [[rhi::FindBackend]].
/// Thread-safety: thread-safe.
const BackendDescriptor* ResolveBackend(Backend backend, char* reason, usize reason_size);

/// Empties the registry, for tests that register fake backends; the next [[rhi::RegisterCompiledBackends]] fills it again.
///
/// Must not be called while a device exists.
///
/// Thread-safety: thread-safe.
void ClearBackendRegistryForTest();

} // namespace engine_placeholder::rhi
