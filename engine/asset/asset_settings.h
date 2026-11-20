#pragma once

//! The asset settings (Phase 22 Day 315): [[asset::RegisterAssetSettings]] declares the byte budgets of the load buffer
//! pool and of the memory backend and (Day 320) the animation error bounds; the readers return them.

#include "asset/resource_id.h"
#include "containers/vector.h"
#include "settings/registry.h"
#include "string/string.h"
#include "types.h"

namespace vse::asset
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

/// The value of `assets.memory_priority` (default 1000): the priority a program mounts its [[asset::HostMemoryBackend]]
/// at, above the mounts of `assets.mounts` (which have the priorities 0, 1, 2, ...). Declared by
/// `RegisterManagerSettings`.
///
/// - Aborts: the setting was not declared.
/// - Thread-safety: thread-safe.
i32 MemoryMountPriorityFromSettings(const settings::Registry& registry);

/// The value of `assets.anim_rotation_error_degrees` (default 0.05): the bound a clip minter holds a clip's rotation
/// error to.
///
/// - Aborts: the setting was not declared.
/// - Thread-safety: thread-safe.
f64 AnimRotationErrorDegreesFromSettings(const settings::Registry& registry);

/// The value of `assets.anim_translation_error` (default 1e-4): the bound on translation and scale error relative to a
/// channel's extent.
///
/// - Aborts: the setting was not declared.
/// - Thread-safety: thread-safe.
f64 AnimTranslationErrorFromSettings(const settings::Registry& registry);

/// Declares the settings of the resource manager: `assets.cache_bytes` (the most bytes of resident resources; 0 for no
/// limit), `assets.allow_fallbacks` (a development setting, default off: a missing or invalid resource of a type that
/// has a registered fallback is replaced by it, visibly and counted), `assets.verify_on_load` (default off: check the
/// content hash of every blob when it is loaded) and `assets.mounts` (the mounts: `PREFIX=PATH` entries separated by
/// `;`, a path being a directory of loose blobs or a pak file; the prefix `/` when none is written; later entries win
/// over earlier ones). A key that is already declared is left as it is.
///
/// - Thread-safety: thread-compatible; call it while the registry is being set up.
/// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
void RegisterManagerSettings(settings::Registry* registry);

/// Declares the settings of the resource server: `assets.net.max_read_bytes` (the most bytes one `Read` returns),
/// `assets.net.max_inflight` (the requests a connection has answered per `Update`; more are answered `Busy`) and
/// `assets.net.page_entries` (the entries of a table or list page). A key that is already declared is left as it is.
///
/// - Thread-safety: thread-compatible; call it while the registry is being set up.
/// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
void RegisterServerSettings(settings::Registry* registry);

/// One entry of `assets.mounts`.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns its strings.
struct MountSpec
{
    String<HeapAllocator> prefix = String<HeapAllocator>(HeapAllocator::Default());  ///< The path prefix, `/` for none.
    String<HeapAllocator> path = String<HeapAllocator>(HeapAllocator::Default());    ///< A directory or a pak file.
    i32 priority = 0;  ///< Higher wins; the entry's index.
};

/// Reads `assets.mounts` into entries (the priority of an entry is its position, so a later one wins).
///
/// - Param registry: the settings; `RegisterManagerSettings` must have been called.
/// - Param out: replaced by the entries.
/// - Fails: `Invalid` for an entry with an empty path; `LimitExceeded` when the setting is longer than 4095 bytes.
/// - Aborts: the setting was not declared.
/// - Thread-safety: thread-safe.
/// - Ownership: `*out` is replaced.
Status MountSpecsFromSettings(const settings::Registry& registry, Vector<MountSpec>* out);

}  // namespace vse::asset
