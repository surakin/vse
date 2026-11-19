#pragma once

// The script demo's world (Phase 27 Day 475): a floor, a crowd of dynamic bodies each with a small Lua behavior
// (wander, flock, flee), a player that walks a square, trigger pickups, and a planner that searches a field with
// `task.parallel_for`. It is the whole frame of the documented order (scripts and merge, physics sync, kinematic drive,
// step, write-back, physics events) with no window or renderer, so a test builds the very same world and compares its
// state hash at any thread count. A window (main.cpp, demo_view.h) adds input, sound, hot reload from an edited file
// and a picture, through the options below; without them the world is the one the tests compare across machines.

#include "asset/resource_manager.h"
#include "audio/clip.h"
#include "audio/mixer.h"
#include "containers/shared_bytes.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/entity.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "jobs/scheduler.h"
#include "physics/world.h"
#include "platform/input.h"
#include "script/backend.h"
#include "script/bindings.h"
#include "script/component_catalog.h"
#include "script/script_reload.h"
#include "script/script_assets.h"
#include "script/script_runtime.h"
#include "script_physics/physics_bindings.h"
#include "script_physics/physics_layers.h"
#include "script_media/audio_services.h"
#include "script_media/input_snapshot.h"
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
    /// Where the minted script is (the build's `asset_mint mint-script` output, mounted at "/"); null runs the built-in
    /// copy of the same text. Either way the module is keyed by [[kDemoScriptId]], so a hot reload names it alike.
    asset::ResourceManager* minted = nullptr;
    /// The player's keys reach the scripts (`input.*`, the arrows and W, A, S, D); off in the tests.
    bool input = false;
    /// The mixer `audio.play` plays into (the pickups' chime); null leaves the scripts without `audio`.
    audio::Mixer* mixer = nullptr;
    /// The mixer's output rate in Hz.
    u32 sample_rate = 48000;
};

/// The resource id of the demo script (`scripts/demo.script.src`), and its module key in hex.
inline constexpr u64 kDemoScriptId = 0x5c27de0000000475ull;
/// The module key of the demo script: [[kDemoScriptId]] as 16 hex digits, the name the dev directory's file has.
inline constexpr const char* kDemoScriptKey = "5c27de0000000475";

/// What a body in the world is, for a view that draws it.
enum class VisualKind : u8
{
    Wander,
    Flock,
    Flee,
    Player,
    Pickup,
    Marker,
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

    /// Whether the world was built: false when the minted script could not be loaded (the reason was printed).
    bool Ok() const { return ok_; }

    /// Runs one frame of 1/60 s: scripts and the merge, the physics sync and step, the write-back, the physics events.
    /// `scheduler` may be null (everything runs on the calling thread). `keys`, with `DemoOptions::input`, is the
    /// window's input of this frame (null: nothing pressed).
    void Frame(jobs::Scheduler* scheduler, const os::InputState* keys = nullptr);

    /// Replaces the demo script with `text` in every partition (hot reload); the instances keep their state.
    ///
    /// - Returns: whether the new text was applied (a text that does not compile is refused and the old one runs on).
    bool Reload(StringView text);

    /// Watches `directory` for an edited copy of the script, the file `<kDemoScriptKey>.lua` (the build writes it with
    /// `asset_mint mint-script --dev-out`); `PollReload` then applies each change.
    ///
    /// - Returns: whether the directory could be watched.
    bool WatchDirectory(StringView directory);
    /// Reloads the script from the watched directory when its file changed since the last poll, between frames.
    ///
    /// - Returns: the reloads applied (0 or 1); a text that does not compile is refused and reported, the old one runs.
    u32 PollReload();
    /// Reloads applied from the watched directory so far.
    u64 FileReloads() const { return file_reloads_; }

    /// Calls `visit(kind, position)` for every body to draw, in creation order (the floor is not listed).
    template <class Visit> void ForEachVisual(Visit&& visit) const
    {
        for (usize i = 0; i < visuals_.Size(); ++i)
        {
            const ecs::LocalTransform* t = world_.Peek<ecs::LocalTransform>(visuals_[i].entity);
            if (t != nullptr)
            {
                visit(visuals_[i].kind, t->position);
            }
        }
    }
    /// The bodies `ForEachVisual` lists.
    usize VisualCount() const { return visuals_.Size(); }
    /// The runtime, for the overlay's measurements (partition load, the profile).
    script::ScriptRuntime& Runtime() { return *runtime_; }

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
    struct Visual
    {
        ecs::Entity entity;
        VisualKind kind = VisualKind::Wander;
    };

    static void OnPickup(void* user, const script::EventRecord& event);
    static audio::AudioClipConstPtr ResolveClip(void* user, StringView name);
    // Reloads the module `key`; `*deferred` says the reloader put it off (a partition was busy), to be tried again.
    bool ApplyReload(StringView key, SharedBytesPtr bytes, usize size, bool* deferred);

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
    UniquePtr<script::ScriptAssets> assets_;
    UniquePtr<script::ScriptDevWatcher> watcher_;
    script_media::InputMap input_map_;
    script_media::InputSnapshot snapshot_;
    os::InputState no_keys_;  // the input of a frame given none
    script_media::InputScriptServices input_services_;
    script_media::VoiceTable voices_;
    script_media::AudioScriptServices audio_services_;
    audio::AudioClipPtr chime_;
    Vector<Visual> visuals_;
    bool ok_ = true;
    u64 file_reloads_ = 0;
    ecs::Entity marker_;
    u64 frames_ = 0;
    u64 pickups_ = 0;
    u64 reload_tick_ = 0;
    u32 entity_count_ = 0;
    f64 last_script_seconds_ = 0.0;
    f64 last_physics_seconds_ = 0.0;
};

}  // namespace script_demo
