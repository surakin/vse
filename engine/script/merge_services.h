// What the merge's `user` pointer carries (Phase 27 Day 455): the services the command kinds need at the sync point. A
// host fills the ones its commands use and passes the struct as the `user` of [[script::Merge]].
#pragma once

#include "ecs/entity.h"
#include "ecs/world.h"
#include "script/component_catalog.h"
#include "script/service_slots.h"
#include "settings/settings.h"
#include "string_view.h"
#include "types.h"

namespace vse::script
{

/// How `entity.spawn(prefab)` fills in a new entity.
///
/// - Thread-safety: thread-compatible plain data; `spawn` runs on the merging thread.
/// - Ownership: `user` is borrowed and must outlive the merge.
struct PrefabSource
{
    /// Adds the prefab's components to `entity` (which already has a transform); returns false for an unknown prefab,
    /// in which case the entity stays as it is.
    bool (*spawn)(void* user, StringView prefab, ecs::World* world, ecs::Entity entity) = nullptr;
    /// Passed to `spawn`.
    void* user = nullptr;
};

/// The services the merge's command kinds use; every pointer may be null when no command needs it.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: borrows everything; it must outlive the merge.
struct MergeServices
{
    /// Where `settings.set` stores values (see [[script::kCommandSettingsSet]]).
    settings::Settings* settings = nullptr;
    /// How `entity.spawn(prefab)` builds an entity from a prefab name.
    const PrefabSource* prefabs = nullptr;
    /// The components `component.set/add/remove` change by stored id.
    const ComponentCatalog* components = nullptr;
    /// What a target above `script` keeps here (see [[script::ServiceSlot]]): its commands read it at the merge and
    /// the runtime hands the same pointers to every partition's bindings. The merge may change what a slot points to;
    /// while the script phase runs it is only read (the scripts run against the tick-start state).
    void* extensions[kMaxServiceExtensions] = {};
};

}  // namespace vse::script
