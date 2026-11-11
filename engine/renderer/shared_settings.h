#pragma once

//! The rendering settings in the engine-wide settings system.
//!
//! [[renderer::RegisterSharedSettings]] declares the built-in rendering settings in a [[settings::Registry]] under the
//! `renderer.` section
//! (`renderer.present_mode`, `renderer.width`, ...), with the types, defaults, ranges and range policies of the
//! renderer's own registry and each setting's [[renderer::CostClass]] as its apply class. A [[settings::Settings]] then
//! loads, layers, validates and saves them, and
//! [[renderer::ApplySharedChange]] is the apply function that hands a change to [[renderer::Renderer::ApplySettings]].
//!
//! The renderer's own [[renderer::RenderSettings]] stays the value type the renderer works with; this header converts
//! to and from the shared system. Files written before the shared system (flat keys, `version = 1`) load through the
//! schema-1 migration.

#include "renderer/render_settings.h"
#include "settings/settings.h"

namespace engine_placeholder::renderer
{

class Renderer;

/// The schema version of the `renderer` section: 1 was the flat-key text of `SerializeSettings`, 2 puts the keys under
/// `renderer.`.
inline constexpr u32 kRendererSchemaVersion = 2;

/// Declares the built-in rendering settings in `registry` as `renderer.<name>` and gives the `renderer` section its
/// schema version.
///
/// - Param registry: an unfrozen registry that holds no `renderer.` settings yet.
/// - Aborts: the registry is frozen, or already holds one of the keys.
/// - Ownership: the registry borrows the setting names (static strings of the renderer).
/// - Thread-safety: not-thread-safe; call at start-up before any other thread uses the registry.
void RegisterSharedSettings(::engine_placeholder::settings::Registry* registry);

/// Adds the migration from the flat-key file of schema 1 (`width = 1920`, no section) to the `renderer.` keys.
///
/// - Ownership: the steps borrow static strings.
/// - Thread-safety: not-thread-safe; call before the settings are loaded.
void AddRendererMigrations(::engine_placeholder::settings::MigrationSet* migrations);

/// Reads the current values of the `renderer.*` settings into a [[renderer::RenderSettings]].
///
/// - Param registry: the registry [[renderer::RegisterSharedSettings]] filled.
/// - Param snapshot: the values to read.
/// - Returns: the settings; any that is not registered keeps its default.
RenderSettings RenderSettingsFromSnapshot(const ::engine_placeholder::settings::Registry& registry,
                                          const ::engine_placeholder::settings::SettingsSnapshot& snapshot);

/// Appends a request for each rendering setting in `settings` that is registered, for [[settings::Settings::Apply]].
///
/// - Ownership: the requests hold values only.
/// - Thread-safety: thread-compatible.
void RenderSettingsToChanges(const RenderSettings& settings, const ::engine_placeholder::settings::Registry& registry,
                             Vector<::engine_placeholder::settings::ChangeRequest>* out);

/// The apply function for the `renderer.*` settings: it builds the requested [[renderer::RenderSettings]] from the
/// renderer's current ones plus the changes and calls [[renderer::Renderer::ApplySettings]]. Pass it to
/// [[settings::Settings::Apply]] with the renderer as `user`.
///
/// A change made while the renderer is inside a frame is queued by the renderer and applied at the next frame; the
/// change is accepted here and can no longer be rolled back through the settings system.
///
/// The requested settings are the values the settings system holds (including changes it accepted earlier that the
/// renderer has queued) plus this batch, so a second change made before the first was applied does not replace it.
///
/// - Returns: false if the renderer could not apply the change (it restored its previous settings), so the settings
/// system rolls the batch back.
/// - Fails: reports the renderer's refusal as false.
/// - Ownership: borrows the renderer for the call.
/// - Thread-safety: owner-thread-only, as [[renderer::Renderer::ApplySettings]].
bool ApplySharedChange(void* renderer, const ::engine_placeholder::settings::ChangeEvent& event);

/// Brings the settings system back in line with the renderer after a queued change failed at the frame boundary: if the
/// renderer's settings in force differ from the values the settings system holds, the renderer's become the settings
/// system's (no apply function runs, listeners are told). Call it after `RenderFrame` in an application that changes
/// settings inside frames.
///
/// - Returns: whether a correction was needed.
/// - Ownership: borrows both for the call.
/// - Thread-safety: owner-thread-only.
bool SyncSharedFromRenderer(Renderer* renderer, ::engine_placeholder::settings::Settings* settings);

/// A [[settings::CapabilityFn]] that asks the renderer's [[renderer::Capabilities]] (`user` is the
/// [[renderer::Renderer]]).
///
/// - Returns: whether the renderer reports the capability.
/// - Fails: never.
/// - Ownership: borrows the renderer for the call.
/// - Thread-safety: owner-thread-only.
bool RendererCapability(void* renderer, const char* capability);

}  // namespace engine_placeholder::renderer
