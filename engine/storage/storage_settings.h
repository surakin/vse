#pragma once

//! The storage settings (Phase 19 Days 257 and 264): [[storage::RegisterStorageSettings]] declares the four limits that bound every decode, the path of the
//! database file, the layout and the autosave interval; [[storage::LimitsFromSettings]] turns the limits into [[storage::CodecLimits]].

#include "settings/registry.h"
#include "mem/heap_allocator.h"
#include "storage/field.h"
#include "string/string.h"

namespace engine_placeholder::storage {

/// The values of `storage.layout`.
enum class StorageLayout : u8 {
    Blob,   ///< One record per entity.
    Columns ///< Also a column table for each queryable component.
};

/// Declares `storage.max_record_bytes`, `storage.max_string_bytes`, `storage.max_array_elements`, `storage.max_components_per_entity`, `storage.path` (text; empty
/// means a private in-memory database), `storage.layout` (`blob` or `columns`) and `storage.autosave_seconds` (0 is off), each with an `ENGINE_STORAGE_*`
/// variable and a `--storage-*` flag. Registering twice is harmless: keys already present are kept.
///
/// Param registry: the registry to declare them in, before it is frozen.
/// Ownership: `registry` is borrowed for the call.
/// Fails: never.
void RegisterStorageSettings(settings::Registry* registry);

/// Returns the current values of the four limits.
///
/// Param registry: a registry in which [[storage::RegisterStorageSettings]] ran.
/// Aborts: the settings are not registered.
/// Ownership: `registry` is borrowed for the call.
CodecLimits LimitsFromSettings(const settings::Registry& registry);

/// Returns the path in `storage.path` (empty when unset).
///
/// Param registry: a registry in which [[storage::RegisterStorageSettings]] ran.
/// Aborts: the settings are not registered.
/// Ownership: `registry` is borrowed for the call; the returned string is owned by the caller.
String<HeapAllocator> PathFromSettings(const settings::Registry& registry);

/// Returns true when `storage.layout` is `columns`.
///
/// Param registry: a registry in which [[storage::RegisterStorageSettings]] ran.
/// Aborts: the settings are not registered.
/// Ownership: `registry` is borrowed for the call.
bool ColumnsFromSettings(const settings::Registry& registry);

/// Returns `storage.autosave_seconds` (0: autosave is off).
///
/// Param registry: a registry in which [[storage::RegisterStorageSettings]] ran.
/// Aborts: the settings are not registered.
/// Ownership: `registry` is borrowed for the call.
u32 AutosaveSecondsFromSettings(const settings::Registry& registry);

} // namespace engine_placeholder::storage
