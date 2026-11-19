#pragma once
//! The runtime of one engine (Phase 23 Day 349): its worlds, its counters and its job system, behind the functions of
//! the C API's table that make and use them. Internal to the module `engine_c`: nothing here reaches the host but
//! through `engine_c.h`.
#include "containers/shared_ptr.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/schedule.h"
#include "ecs/world.h"
#include "engine/engine_c.h"
#include "engine_c/host_texture.h"
#include "engine_c/script_api.h"
#include "jobs/job.h"
#include "audio/spatial_audio.h"
#include "physics/character.h"
#include "physics/world.h"
#include "string_view.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"
#include "types.h"

namespace engine_placeholder::jobs
{
class Scheduler;
}

namespace engine_placeholder::engine_c
{

/// The component of ENGINE_COMPONENT_CAMERA (Phase 24 Day 360).
///
/// - Thread-safety: plain data.
struct CameraComponent
{
    u32 flags = 0;            ///< ENGINE_CAMERA_* bits.
    f32 fov_y = 1.0471976f;   ///< Vertical field of view in radians.
    f32 near_plane = 0.1f;    ///< Distance to the near plane.
    f32 far_plane = 1000.0f;  ///< Distance to the far plane.
    f32 aspect = 1.0f;        ///< Width over height, used with ENGINE_CAMERA_FIXED_ASPECT.
};

/// The component of ENGINE_COMPONENT_LIGHT (Phase 24 Day 361).
///
/// - Thread-safety: plain data.
struct LightComponent
{
    u32 kind = 1;                       ///< ENGINE_LIGHT_*.
    f32 color[3] = {1.0f, 1.0f, 1.0f};  ///< Linear RGB.
    f32 intensity = 1.0f;               ///< Scale of the colour.
    f32 radius = 10.0f;                 ///< Reach of a point light.
};

/// The component of ENGINE_COMPONENT_VISIBILITY (Phase 24 Day 362).
///
/// - Thread-safety: plain data.
struct VisibilityComponent
{
    u32 hidden = 0;  ///< 1 when the entity (and its children) are not drawn.
};

/// An entity that follows a joint of another entity's skeleton (Phase 24 Day 363).
///
/// - Thread-safety: plain data.
struct JointAttachment
{
    u64 skeleton_key = 0;  ///< The entity (key) whose skeleton instance holds the joint.
    u32 joint = 0;
};

/// The slot overrides of one entity (Phase 24 Day 362): at most 8, replaced by slot name.
///
/// - Thread-safety: plain data.
struct SlotOverrides
{
    static constexpr u32 kMax = 8;
    struct Item
    {
        char name[24] = {};
        u32 kind = 0;  ///< ENGINE_SLOT_*.
        f32 value[4] = {};
        u64 texture = 0;
        u64 host_texture = 0;  ///< The value of an engine_handle of a host texture (ENGINE_SLOT_HOST_TEXTURE).
        /// The mesh slot (index range) the override applies to; `kAllMeshSlots` for every one.
        u32 mesh_slot = 0xFFFFFFFFu;
    };
    static constexpr u32 kAllMeshSlots = 0xFFFFFFFFu;
    u32 count = 0;
    Item items[kMax];
};

/// The materials of an entity's mesh slots (Phase 24 Day 377): slot `i` draws with `material[i]`, or with the mesh
/// reference's material when that is 0. Set by entity_set_mesh_slot_material.
///
/// - Thread-safety: plain data.
struct SlotMaterialsComponent
{
    static constexpr u32 kMax = 16;
    u32 count = 0;  ///< One past the last slot that has a material.
    u64 material[kMax] = {};
};

/// The component of ENGINE_COMPONENT_LABEL (Phase 24 Day 367).
///
/// - Thread-safety: plain data.
struct LabelComponent
{
    u32 length = 0;  ///< Bytes of `text`.
    char text[ENGINE_LABEL_MAX_BYTES] = {};
    f32 color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    f32 size = 16.0f;
    f32 offset[3] = {};
    u32 anchor = 0;
    u32 flags = 0;
};

/// What surrounds a world (engine_environment with the defaults of its documentation).
///
/// - Thread-safety: plain data; guarded by the world mutex.
struct Environment
{
    f32 ambient_color[3] = {1.0f, 1.0f, 1.0f};
    f32 ambient_intensity = 0.2f;
    f32 sky_color[3] = {0.0f, 0.0f, 0.0f};
    f32 exposure = 1.0f;
};

/// The component of ENGINE_COMPONENT_MESH_REFERENCE: what an entity draws.
///
/// - Thread-safety: plain data.
struct MeshReference
{
    u64 mesh = 0;               ///< The resource id of the mesh (0: nothing to draw).
    u32 material = 0;           ///< The host's own number, kept and returned (the engine does not read it).
    u64 material_resource = 0;  ///< The resource id of the material (0: the surface's default material).
};

/// The component of ENGINE_COMPONENT_CHARACTER (Phase 25 Day 409): the shape and rules of a character. The controller
/// itself is in `WorldObject::characters`.
///
/// - Thread-safety: plain data.
struct CharacterComponent
{
    f32 radius = 0.4f;
    f32 height = 1.8f;
    f32 step_height = 0.3f;
    f32 min_ground_y = 0.64f;
    f32 skin_width = 0.02f;
    f32 gravity_scale = 1.0f;
};

class AudioHost;
/// A shared handle to the audio host: the runtime holds one, and every table function in flight holds one, so
/// `audio_close` never frees a host another thread is inside.
using AudioHostPtr = SharedPtr<AudioHost>;

/// A character controller of a world (Phase 25 Day 409): the entity it belongs to and the controller of its capsule.
///
/// - Thread-safety: thread-compatible; the runtime's world mutex guards every use.
struct CharacterEntry
{
    u64 key = 0;  ///< The entity (its u64 form).
    physics::CharacterController controller;
    physics::CharacterConfig config;
    physics::CharacterEvents events;  ///< What the last character_move found.
};

/// A world and the systems that run on it.
///
/// - Thread-safety: thread-compatible; the runtime's world mutex guards every use.
struct WorldObject
{
    WorldObject();
    ~WorldObject();
    ecs::World world;
    ecs::Schedule schedule;
    Vector<CharacterEntry> characters;  ///< The characters, in the order they were made (Day 409).
    bool physics_used = false;     ///< Whether a body component was set: the frame makes bodies only then (Day 408).
    Environment environment;       ///< What world_set_environment set (the defaults until then).
    bool environment_set = false;  ///< Whether it was called (no default sun once it was).
    bool audio_used = false;  ///< Whether an audio component was set: the frame places sounds only then (Phase 26).
    /// The world's sounds and the audio host they play on (the share keeps its mixer alive); remade when the host
    /// changes.
    UniquePtr<audio::SpatialAudio> spatial_audio;
    AudioHostPtr spatial_host;
    /// The world's scripts (Phase 27 Day 471): made by the first `script_load`, script component or host function.
    UniquePtr<ScriptHost> scripts;
};

/// A count of jobs not yet finished.
///
/// - Thread-safety: thread-safe; `pending` is read and written under `mutex`.
struct CounterObject
{
    Mutex mutex;
    ConditionVariable changed;
    u64 pending = 0;
    u32 waiters = 0;  // guarded by the runtime's table mutex, not by `mutex`

    /// Counts a job that ended and wakes the waiters when none is left.
    ///
    /// - Thread-safety: thread-safe.
    /// - Fails: never.
    void Done();
};

/// The job system of an engine: a scheduler on a thread of its own (participant 0 sleeps between jobs, it is not the
/// host's thread), started by the first job submitted and stopped when the engine is destroyed.
///
/// - Thread-safety: `Submit` is thread-safe; the constructor and destructor belong to the runtime's lifetime.
class JobHost
{
public:
    /// Starts the thread and waits until the scheduler runs.
    JobHost();
    /// Stops the scheduler and joins the thread. Every counter is at zero by then (the engine refuses to be destroyed
    /// otherwise).
    ~JobHost();
    JobHost(const JobHost&) = delete;
    JobHost& operator=(const JobHost&) = delete;

    /// Queues a job.
    ///
    /// - Thread-safety: thread-safe.
    void Submit(jobs::Job&& job);

    /// The scheduler (it runs from the constructor's return until the destructor).
    ///
    /// - Thread-safety: thread-safe.
    jobs::Scheduler* GetScheduler() const { return scheduler_; }

private:
    static void Main(JobHost* self);

    Mutex mutex_;
    ConditionVariable ready_changed_;
    bool ready_ = false;
    Atomic<int> stop_{0};
    jobs::Scheduler* scheduler_ = nullptr;
    Thread thread_;
};

class ResourceHost;
class SurfaceHost;

/// The limits of a glTF import, read from the `assets.import.*` settings when the engine is created (all 0 when the
/// importer is not built in).
///
/// - Thread-safety: thread-compatible plain data.
struct ImportLimitValues
{
    u64 max_file_bytes = 0;    ///< `assets.import.max_file_bytes`.
    u64 max_json_bytes = 0;    ///< `assets.import.max_json_bytes`.
    u64 max_buffer_bytes = 0;  ///< `assets.import.max_buffer_bytes`.
    u64 max_output_bytes = 0;  ///< `assets.import.max_output_bytes`.
    u64 max_pixels = 0;        ///< `assets.import.max_pixels`.
    u32 max_nodes = 0;         ///< `assets.import.max_nodes`.
    u32 max_accessors = 0;     ///< `assets.import.max_accessors`.
    u32 max_images = 0;        ///< `assets.import.max_images`.
};

/// The settings the resource host is made from (read when the engine is created).
///
/// - Thread-safety: thread-compatible plain data.
struct ResourceConfig
{
    u64 memory_bytes = u64{512} << 20;       ///< `assets.memory_backend_bytes`.
    i32 priority = 1000;                     ///< `assets.memory_priority`.
    u64 buffer_pool_bytes = u64{256} << 20;  ///< `assets.buffer_pool_bytes`.
    u64 cache_bytes = 0;                     ///< `assets.cache_bytes`.
    bool allow_fallbacks = false;            ///< `assets.allow_fallbacks`.
    bool verify_on_load = false;             ///< `assets.verify_on_load`.
    u32 max_inflight = 4;                    ///< `assets.max_inflight`.
    u32 max_queued = 256;                    ///< `assets.max_queued`.
    ImportLimitValues import_limits;  ///< The limits of a glTF import (`assets.import.*`); all 0 without the importer.
};

/// What an engine holds of worlds, counters and jobs.
///
/// - Thread-safety: thread-safe; see the mutexes.
class Runtime
{
public:
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /// The world calls and the frame run under this mutex: they are serialized with each other.
    Mutex world_mutex;
    struct WorldSlot
    {
        UniquePtr<WorldObject> object;
        u32 generation = 0;
    };
    Vector<WorldSlot> worlds;  // guarded by world_mutex
    u64 frames = 0;            // guarded by world_mutex
    usize cursor = 0;  // the world a budgeted frame stopped before: the next frame starts there; guarded by world_mutex

    /// Guards `counters`, the counters' `waiters` and the start of `jobs`.
    Mutex table_mutex;
    struct CounterSlot
    {
        UniquePtr<CounterObject> object;
        u32 generation = 0;
    };
    Vector<CounterSlot> counters;
    UniquePtr<JobHost> jobs;

    /// Starts the job system if it is not running and returns it.
    ///
    /// - Thread-safety: thread-safe.
    JobHost& EnsureJobs();

    /// The host textures of the engine (Phase 24 Day 365).
    TextureHost textures;

    /// The font file the labels are drawn with, and how many times it was set (Phase 24 Day 367).
    ///
    /// - Param out: receives a copy of the bytes (empty when none was set).
    /// - Returns: the version, 0 while none was set.
    u64 CopyLabelFont(Vector<u8>* out);
    /// Replaces the label font.
    void SetLabelFont(const void* data, usize size);

    /// What the resource host is made from; set once when the engine is created, before any call.
    ResourceConfig resource_config;

    /// What the `script.*` settings said when the engine was created; a world's script runtime is made from it.
    ScriptSettingValues script_settings;

    /// The host-driven audio (Phase 26 Day 438): made by `audio_open`, destroyed by `audio_close` or with the engine.
    /// `audio_host` is what the audio thread reads; `audio_owner` owns it; both change under `audio_mutex`.
    Mutex audio_mutex;
    AudioHostPtr audio_owner;
    Atomic<AudioHost*> audio_host{nullptr};

    /// The step configuration every new world's physics starts with (`physics.*` settings, read when the engine is
    /// created).
    physics::StepConfig physics_config;

private:
    Mutex label_font_mutex_;
    Vector<u8> label_font_;
    u64 label_font_version_ = 0;

public:
    /// The resources of the engine: made by the first call that needs them.
    ///
    /// - Thread-safety: thread-safe.
    ResourceHost* Resources();

    /// The resources if they exist, else null.
    ///
    /// - Thread-safety: thread-safe.
    ResourceHost* ResourcesIfCreated();

    /// The surfaces of the engine (Day 352): made by the first call that needs them.
    ///
    /// - Thread-safety: thread-safe.
    SurfaceHost* Surfaces();

    /// The surfaces if they exist, else null.
    ///
    /// - Thread-safety: thread-safe.
    SurfaceHost* SurfacesIfCreated();
    /// Jobs submitted and not yet run to their end. The scheduler aborts when its queues are full, so a submit that
    /// would pass `kMaxJobsInFlight` is refused.
    Atomic<u32> jobs_in_flight{0};
    static constexpr u32 kMaxJobsInFlight = 2048;

    // Declared last, so it is destroyed first: its lent buffers are handed back on the job system's worker, and its
    // loads read on the job system's scheduler.
    UniquePtr<ResourceHost> resources;  // guarded by table_mutex at creation
    // After the resources: destroyed before them (a surface holds a device and a swapchain, nothing of the content).
    UniquePtr<SurfaceHost> surfaces;  // guarded by table_mutex at creation
};

// The functions of the table that the runtime serves (engine_c.cpp lists them in the table).
engine_status FrameFn(engine_handle engine, const engine_frame_info* info, engine_frame_stats* stats);
engine_status WorldCreateFn(engine_handle engine, engine_handle* out);
engine_status WorldDestroyFn(engine_handle world);
engine_status EntityCreateFn(engine_handle world, u64* out_key);
engine_status EntityDestroyFn(engine_handle world, u64 key);
engine_status ComponentSetFn(engine_handle world, u64 key, u32 kind, const void* value);
engine_status ComponentGetFn(engine_handle world, u64 key, u32 kind, void* out);
engine_status WorldSetEnvironmentFn(engine_handle world, const engine_environment* environment);
engine_status EntitySetParentFn(engine_handle world, u64 child_key, u64 parent_key);
engine_status EntityGetParentFn(engine_handle world, u64 key, u64* parent_key);
engine_status EntityFindFn(engine_handle world, engine_string name, u64* key);
engine_status EntityFindAllFn(engine_handle world, engine_string name, u64* keys, size_t capacity, size_t* total);
engine_status EntitySetMeshSlotOverrideFn(engine_handle world, u64 key, engine_string mesh_slot, engine_string slot,
                                          const engine_slot_override* value);
engine_status EntitySetSlotOverrideFn(engine_handle world, u64 key, engine_string slot,
                                      const engine_slot_override* value);
engine_status EntityClearSlotOverrideFn(engine_handle world, u64 key, engine_string slot);
engine_status AnimationPlayFn(engine_handle world, u64 key, u64 clip, const engine_play_params* params);
engine_status AnimationSetSpeedFn(engine_handle world, u64 key, float speed);
engine_status AnimationStopFn(engine_handle world, u64 key);
engine_status AnimationTimeFn(engine_handle world, u64 key, double* seconds);
engine_status SkeletonFindJointFn(engine_handle world, u64 key, engine_string name, u32* index);
engine_status SkeletonJointTransformFn(engine_handle world, u64 key, u32 joint, float* matrix);
engine_status EntityAttachToJointFn(engine_handle world, u64 key, u64 skeleton_key, u32 joint);
engine_status LevelSpawnFn(engine_handle world, u64 level, const engine_spawn_params* params, u64* root, u64* keys,
                           size_t capacity, size_t* total);
engine_status PhysicsSetGravityFn(engine_handle world, const float* gravity);
engine_status BodyApplyForceFn(engine_handle world, u64 key, const float* force, const float* point);
engine_status BodyApplyImpulseFn(engine_handle world, u64 key, const float* impulse, const float* point);
engine_status BodySetVelocityFn(engine_handle world, u64 key, const float* linear, const float* angular);
engine_status BodyGetVelocityFn(engine_handle world, u64 key, float* linear, float* angular);
engine_status BodyTeleportFn(engine_handle world, u64 key, const float* position, const float* rotation);
engine_status PhysicsRaycastFn(engine_handle world, const float* origin, const float* direction, float max_distance,
                               u32 layer_mask, engine_ray_hit* hit, int* found);
engine_status PhysicsOverlapSphereFn(engine_handle world, const float* center, float radius, u32 layer_mask, u64* keys,
                                     size_t capacity, size_t* total);
engine_status PhysicsOverlapBoxFn(engine_handle world, const float* center, const float* half_extents, u32 layer_mask,
                                  u64* keys, size_t capacity, size_t* total);
engine_status JointReactionFn(engine_handle world, u64 key, float* force, float* torque);
engine_status CharacterMoveFn(engine_handle world, u64 key, const float* velocity, float dt,
                              engine_character_state* state);
engine_status CharacterJumpFn(engine_handle world, u64 key, float speed, engine_character_state* state);
engine_status PhysicsEventsFn(engine_handle world, engine_physics_event* out, size_t capacity, size_t* total);
engine_status PhysicsSnapshotFn(engine_handle world, engine_buffer* out);
engine_status PhysicsRestoreFn(engine_handle world, const void* data, size_t size);
engine_status EntitySetMeshSlotMaterialFn(engine_handle world, u64 key, u32 slot, u64 material_resource);
engine_status EntitySetMeshSlotMaterialByNameFn(engine_handle world, u64 key, engine_string slot,
                                                u64 material_resource);
engine_status EntityClearMeshSlotMaterialsFn(engine_handle world, u64 key);
engine_status ComponentRemoveFn(engine_handle world, u64 key, u32 kind);
engine_status ComponentHasFn(engine_handle world, u64 key, u32 kind, int* out);
engine_status LabelFontSetFn(engine_handle engine, const void* data, size_t size);
engine_status EntityNameSetFn(engine_handle world, u64 key, engine_string name);
engine_status EntityNameGetFn(engine_handle world, u64 key, engine_buffer* out);
engine_status QueryFn(engine_handle world, u32 mask, u64* keys, size_t capacity, size_t* total);
engine_status CounterCreateFn(engine_handle engine, engine_handle* out);
engine_status CounterDestroyFn(engine_handle counter);
engine_status JobsSubmitFn(engine_handle engine, engine_job_fn fn, void* user, engine_handle counter);
engine_status CounterValueFn(engine_handle counter, u64* out);
engine_status CounterWaitFn(engine_handle counter, u64 timeout_ms);

/// Makes a text buffer (a NUL after the bytes) the host releases with `engine_buffer_release`.
///
/// - Thread-safety: thread-safe.
/// - Fails: never.
void MakeTextBuffer(StringView text, engine_buffer* out);

/// Makes a buffer of `size` writable bytes (a NUL after them) the host releases with `engine_buffer_release`, and
/// returns its bytes; null (and `out` untouched) when memory ran out.
///
/// - Thread-safety: thread-safe.
/// - Fails: returns null when memory ran out.
char* MakeBlankBuffer(usize size, engine_buffer* out);

/// The world a handle names, or null when the handle is stale or not a world's (the failure text is set as for any
/// handle).
///
/// - Thread-safety: the caller holds `runtime.world_mutex`.
/// - Param status: when not null, receives ENGINE_OK or the failure of the handle.
WorldObject* FindWorld(Runtime& runtime, engine_handle handle, engine_status* status = nullptr);

}  // namespace engine_placeholder::engine_c
