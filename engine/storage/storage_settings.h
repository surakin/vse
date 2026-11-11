#pragma once

//! The storage settings (Phase 19 Day 257): [[storage::RegisterStorageSettings]] declares the four limits that bound every decode, and
//! [[storage::LimitsFromSettings]] turns them into [[storage::CodecLimits]]. Day 264 adds the rest (path, layout, autosave).

#include "settings/registry.h"
#include "storage/field.h"

namespace engine_placeholder::storage {

/// Declares `storage.max_record_bytes`, `storage.max_string_bytes`, `storage.max_array_elements` and `storage.max_components_per_entity` (each with an
/// `ENGINE_STORAGE_*` variable and a `--storage-*` flag). Registering twice is harmless: keys already present are kept.
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

} // namespace engine_placeholder::storage
