#pragma once

// The script demo's world (Phase 27 Day 475): a floor, a crowd of dynamic bodies each with a small Lua behavior
// (wander, flock, flee), a player that walks a square, trigger pickups, and a planner that searches a field with
// `task.parallel_for`. It is the whole frame of the documented order (scripts and merge, physics sync, kinematic drive,
// step, write-back, physics events) with no window or renderer, so a test builds the very same world and compares its
// state hash at any thread count.

#include "containers/shared_bytes.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/entity.h"
#include "ecs/world.h"
#include "jobs/scheduler.h"
#include "physics/world.h"
#include "script/backend.h"
#include "script/bindings.h"
#include "script/component_catalog.h"
#include "script/script_reload.h"
#include "script/script_runtime.h"
#include "script_physics/physics_bindings.h"
#include "script_physics/physics_layers.h"
#include "storage/component_registry.h"
#include "string_view.h"
#include "types.h"

namespace script_demo
{

using namespace engine_placeholder;

/// What to build.
struct DemoOptions
{
    /// Bodies with a behavior (a third each of wander, flock and flee).
    u32 entities = 2000;
    /// Script partitions (the scripts' work is cut into this many pieces whatever the thread count).
    u32 partitions = 16;
    /// Seeds the scripts' random streams.
    u32 seed = 1;
    /// Cells the planner searches each round (a multiple of 256).
    u32 field_cells = 16384;
};

/// The built-in copy of `scripts/demo.lua` (the same text, embedded by the build).
///
/// - Returns: a NUL-terminated string.
/// - Ownership: static.
const char* BuiltInScript();

class DemoWorld
{
public:
    explicit DemoWorld(const DemoOptions& options);
    ~DemoWorld();
    DemoWorld(const DemoWorld&) = delete;
    DemoWorld& operator=(const DemoWorld&) = delete;

    /// Runs one frame of 1/60 s: scripts and the merge, the physics sync and step, the write-back, the physics events.
    /// `scheduler` may be null (everything runs on the calling thread).
    void Frame(jobs::Scheduler* scheduler);

    /// Replaces the demo script with `text` in every partition (hot reload); the instances keep their state.
    ///
    /// - Returns: whether the new text was applied (a text that does not compile is refused and the old one runs on).
    bool Reload(StringView text);

    /// A hash of everything the run decided: the physics state, the marker, the pickup counts and the script counters.
    u64 StateHash() const;

    u64 Frames() const { return frames_; }
    const script::ScriptRuntimeStats& ScriptStats() const { return runtime_->Stats(); }
    /// Pickup events the game received (`pickup_collected`).
    u64 PickupsCollected() const { return pickups_; }
    /// Where the planner put the marker.
    Vec3 MarkerPosition() const;
    /// Seconds the last frame spent in the scripts and in the physics.
    f64 LastScriptSeconds() const { return last_script_seconds_; }
    f64 LastPhysicsSeconds() const { return last_physics_seconds_; }
    u32 EntityCount() const { return entity_count_; }

private:
    static void OnPickup(void* user, const script::EventRecord& event);

    DemoOptions options_;
    storage::ComponentRegistry registry_;
    script::ComponentCatalog catalog_;
    script::BindingRegistry bindings_;
    UniquePtr<script::Backend> backend_;
    ecs::World world_;
    physics::PhysicsWorld physics_;
    script_physics::PhysicsLayers layers_;
    script_physics::PhysicsScriptServices services_;
    UniquePtr<script::ScriptRuntime> runtime_;
    UniquePtr<script::ScriptReloader> reloader_;
    ecs::Entity marker_;
    u64 frames_ = 0;
    u64 pickups_ = 0;
    u64 reload_tick_ = 0;
    u32 entity_count_ = 0;
    f64 last_script_seconds_ = 0.0;
    f64 last_physics_seconds_ = 0.0;
};

}  // namespace script_demo
