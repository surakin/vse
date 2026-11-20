// Minted scripts in a script world (Phase 27 Day 463): scripts load through the resource manager like every other
// asset, with their dependency closure, and are added to the world as modules (docs/guide/scripting.md, "Minted
// scripts").
#pragma once

#include "asset/cpu_loaders.h"
#include "asset/resource_manager.h"
#include "containers/vector.h"
#include "script/script_world.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace vse::script
{

/// Loads script assets through a resource manager and adds them to a script world: the closure first (every script a
/// script requires, before it), each once, atomically: when the manager cannot load a script or one of its
/// dependencies (a missing dependency, a cycle, a damaged blob), nothing of that closure is added and the failure is
/// returned. The source text stays in the manager's loaded buffer, shared by every partition's state (zero copy); the
/// object holds the manager's handles, so the scripts stay loaded until [[script::ScriptAssets::Release]] or the
/// destruction, and a state keeps what it compiled after that until it is reset.
///
/// The manager must have the script loader (`asset::RegisterCpuLoaders`).
///
/// - Thread-safety: thread-compatible: one thread adds at a time (the world's `AddModule` is thread-safe).
/// - Ownership: borrows the world and the manager, which must outlive it; owns the handles.
class ScriptAssets
{
public:
    /// Makes the set over `world` and `manager`; both not null.
    ///
    /// - Ownership: borrows both; they must outlive the set.
    ScriptAssets(ScriptWorld* world, asset::ResourceManager* manager);

    /// Loads script `id` and its closure and adds what is not in the world yet; the scripts run in each partition
    /// when it next loads its scripts, dependencies first. Adding an id twice does nothing the second time.
    ///
    /// - Param id: the script's resource id.
    /// - Param failure: receives the manager's failure chain; may be null.
    /// - Returns: `Ok`, or what the manager reported (`NotFound`, `MissingDependency`, `DependencyCycle`,
    /// `DependencyTypeMismatch`, `Corrupt`, `UnsupportedVersion`).
    /// - Ownership: borrows `failure` for the call.
    /// - Fails: as described; nothing is added then.
    asset::Status Add(asset::ResourceId id, asset::LoadFailure* failure = nullptr);

    /// The number of scripts added to the world.
    ///
    /// - Fails: never.
    usize Count() const { return added_.Size(); }

    /// The ids of the scripts added, in the order they were added (dependencies before dependents).
    ///
    /// - Ownership: borrowed; valid until the next `Add`.
    /// - Fails: never.
    const Vector<asset::ResourceId>& Added() const { return added_; }

    /// The id of the script that defines the behavior `name`, or none (the behaviors a script defines are the entry
    /// points its minter found).
    ///
    /// - Param name: a behavior name.
    /// - Fails: never.
    asset::ResourceId ScriptOf(StringView name) const;

    /// Drops the manager's hold on every script (they may unload when nothing else holds them); the world's states
    /// keep what they compiled.
    ///
    /// - Fails: never.
    void Release();

private:
    struct Entry
    {
        asset::ResourceId id;
        String<HeapAllocator> name{HeapAllocator::Default()};
    };

    ScriptWorld* world_;
    asset::ResourceManager* manager_;
    Vector<asset::Handle<asset::ScriptView>> held_;
    Vector<asset::ResourceId> added_;
    Vector<Entry> behaviors_;
};

}  // namespace vse::script
