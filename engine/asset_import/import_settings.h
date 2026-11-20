#pragma once

//! The import settings (Phase 22 Day 339): [[asset::RegisterImportSettings]] declares the limits of the glTF importer
//! as `assets.import.*` settings, and [[asset::ImportLimitsFromSettings]] reads them into an [[asset::ImportLimits]].

#include "asset_import/gltf_import.h"
#include "settings/registry.h"

namespace engine_placeholder::asset
{

/// Declares `assets.import.max_file_bytes`, `max_json_bytes`, `max_buffer_bytes`, `max_nodes`, `max_accessors`,
/// `max_images`, `max_pixels`, `max_output_bytes`, `max_collision_triangles`, `light_intensity_scale`,
/// `sun_intensity_scale` and `light_cutoff` (flags `assets-import-*`, environment `ENGINE_ASSETS_IMPORT_*`), once: a
/// second call changes nothing.
///
/// - Param registry: the registry to declare them in.
/// - Ownership: the registry keeps what it is given.
/// - Fails: never.
void RegisterImportSettings(settings::Registry* registry);

/// The limits the settings give; a limit with no setting keeps its default.
///
/// - Param registry: a registry in which [[asset::RegisterImportSettings]] ran.
/// - Returns: the limits.
/// - Ownership: borrows `registry` for the call only.
/// - Fails: never.
ImportLimits ImportLimitsFromSettings(const settings::Registry& registry);

/// Reads `assets.import.light_intensity_scale`, `sun_intensity_scale` and `light_cutoff` (glTF coverage Day 12; flags
/// `assets-import-*`, environment `ENGINE_ASSETS_IMPORT_*`) into `options`; a setting that is not declared leaves its
/// field as it is.
///
/// - Param registry: a registry in which [[asset::RegisterImportSettings]] ran.
/// - Param options: receives the three fields.
/// - Ownership: borrows both for the call only.
/// - Fails: never.
void ApplyImportLightSettings(const settings::Registry& registry, ImportOptions* options);

}  // namespace engine_placeholder::asset
