#pragma once
//! The runtime of one engine: its worlds, its counters and its job system, behind the functions of
//! the C API's table that make and use them. Internal to the module `engine_c`: nothing here reaches the host but
//! through `engine_c.h`.
#include "asset/content_hash.h"
#include "containers/shared_ptr.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/schedule.h"
#include "ecs/world.h"
#include "engine/engine_c.h"
#include "engine_c/host_texture.h"
#include "engine_c/instances.h"
#include "engine_c/script_api.h"
#include "jobs/job.h"
#include "audio/spatial_audio.h"
#include "physics/character.h"
#include "physics/character_set.h"
#include "math/quaternion.h"
#include "physics/world.h"
#include "string_view.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"
#include "types.h"

namespace vse::jobs
{
class Scheduler;
}

namespace vse::engine_c
{

/// The component of ENGINE_COMPONENT_CAMERA.
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

/// The component of ENGINE_COMPONENT_LIGHT.
///
/// - Thread-safety: plain data.
struct LightComponent
{
    u32 kind = 1;                       ///< ENGINE_LIGHT_*.
    f32 color[3] = {1.0f, 1.0f, 1.0f};  ///< Linear RGB.
    f32 intensity = 1.0f;               ///< Scale of the colour.
    f32 radius = 10.0f;                 ///< Reach of a point or spot light.
    f32 inner_cone = 0.0f;              ///< A spot light's full-strength half-angle, radians.
    f32 outer_cone = 0.7853981634f;     ///< A spot light's outer half-angle, radians.
    /// The axis toward the light in the entity's own space, for a directional or spot light: +Z for a light the host
    /// sets (it shines along -Z); a spawned level keeps its light's own axis (maintenance, 2026-11-20).
    f32 axis[3] = {0.0f, 0.0f, 1.0f};
};

/// The component of ENGINE_COMPONENT_VISIBILITY.
///
/// - Thread-safety: plain data.
struct VisibilityComponent
{
    u32 hidden = 0;  ///< 1 when the entity (and its children) are not drawn.
};

/// An entity that follows a joint of another entity's skeleton.
///
/// - Thread-safety: plain data.
struct JointAttachment
{
    u64 skeleton_key = 0;  ///< The entity (key) whose skeleton instance holds the joint.
    u32 joint = 0;
};

/// The slot overrides of one entity: at most 8, replaced by slot name.
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

/// The materials of an entity's mesh slots: slot `i` draws with `material[i]`, or with the mesh
/// reference's material when that is 0. Set by entity_set_mesh_slot_material.
///
/// - Thread-safety: plain data.
struct SlotMaterialsComponent
{
    static constexpr u32 kMax = 16;
    u32 count = 0;  ///< One past the last slot that has a material.
    u64 material[kMax] = {};
};

/// The component of ENGINE_COMPONENT_LABEL.
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
    f32 reflection_intensity = 0.0f;
    u32 sky_mode = 0;  // ENGINE_SKY_*
    f32 sky_zenith[3] = {0.18f, 0.35f, 0.75f};
    f32 sky_horizon[3] = {0.65f, 0.75f, 0.9f};
    f32 sky_ground[3] = {0.25f, 0.24f, 0.22f};
    f32 sun_direction[3] = {0.4f, 0.6f, 0.3f};
    f32 sun_color[3] = {8.0f, 7.2f, 6.0f};
    f32 sun_size = 0.05f;
    f32 sky_horizon_blend = 0.1f;
    f32 sky_exposure = 1.0f;
    f32 sky_ambient_intensity = 1.0f;
    f32 sky_rotation = 0.0f;  // radians about +Y, of a cube sky
    u64 sky_cube = 0;         // the texture resource of a cube sky (0: none), set by `world_set_sky_cube`
};

/// Whether an environment can be stored and drawn: every field finite, the colours and intensities not negative, the
/// exposure above 0 and, for a procedural sky, a sun that exists (non-zero direction, size in (0, pi / 2]), a horizon
/// blend in (0, 1] and a sky exposure above 0.
///
/// - Fails: never.
/// - Thread-safety: thread-safe. A pure function of its argument.
bool EnvironmentUsable(const Environment& e);

/// The component of ENGINE_COMPONENT_NAME: what `entity_name_set` writes and `level_spawn` gives the nodes of a level
/// (`name_prefix` + `/` + the node's name); `entity_find` and the scripts' `entity.find` and `entity.name` read it.
///
/// - Thread-safety: plain data.
struct EntityName
{
    u8 length = 0;
    char text[ENGINE_NAME_MAX_BYTES] = {};
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

/// The component of ENGINE_COMPONENT_TERRAIN: a height-map terrain and its surface.
///
/// - Thread-safety: plain data.
struct TerrainComponent
{
    u64 height_map = 0;     ///< The resource id of the height map (`HGHT`).
    u64 layer_texture = 0;  ///< The resource id of the layer array texture, or 0.
    u64 splat_texture = 0;  ///< The resource id of the splat map, or 0.
    u32 flags = 0;          ///< `ENGINE_TERRAIN_*`.
    u32 layer_count = 1;    ///< The layers used, 1 to 4.
    i32 slope_layer = -1;   ///< The layer steep faces use, or -1.
    f32 slope_start = 0.35f;
    f32 slope_end = 0.55f;
    f32 fade_start = 150.0f;
    f32 fade_end = 300.0f;
    f32 tiling[4] = {8.0f, 8.0f, 8.0f, 8.0f};
    f32 tint[4][3] = {{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};
    f32 roughness[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    f32 metallic[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    f32 friction = 0.5f;     ///< The collider's friction.
    f32 restitution = 0.0f;  ///< The collider's bounciness.
    bool made_body = false;  ///< The static body was made with the terrain (and goes with it).
};

/// The component of ENGINE_COMPONENT_CHARACTER: the shape and rules of a character. The controller
/// itself is in the world's `physics::CharacterSet` (`WorldObject::characters`), shared with the scripts' `character.*`
/// (C API script physics, 2026-11-20).
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

/// The unscaled shape of a static collider. The frame makes the `physics::Collider` from it at
/// its end, once the entity's world matrix is known, with the world scale of that moment; a later scale change is
/// counted and not followed.
///
/// - Thread-safety: plain data.
struct ColliderSource
{
    u32 shape = 0;  ///< `asset::StaticColliderShape` values: mesh, box, sphere, capsule; 4 a height field.
    u64 mesh = 0;   ///< The `MCOL` of a mesh shape, or the `HGHT` of a height field.
    Vec3 half_extents = Vec3(0.0f, 0.0f, 0.0f);    ///< A box's half sizes, unscaled.
    f32 radius = 0.0f;                             ///< A sphere's or a capsule's radius, unscaled.
    f32 half_height = 0.0f;                        ///< Half a capsule's straight part, unscaled.
    Vec3 offset = Vec3(0.0f, 0.0f, 0.0f);          ///< Where the shape sits on the body, unscaled.
    Quaternion rotation = Quaternion::Identity();  ///< The shape's turn on the body.
    f32 friction = 0.5f;                           ///< The surface's friction.
    f32 restitution = 0.0f;                        ///< The surface's bounciness.
    f32 density = 1000.0f;                         ///< Kilograms per cubic metre.
    u32 layer = 0;                                 ///< The collision layer.
    u32 group = 1;                                 ///< The groups it is in.
    u32 mask = 0xFFFFFFFFu;                        ///< The groups it meets.
    bool is_trigger = false;                       ///< Reports overlaps and does not collide.
    u8 state = 0;                                  ///< 0 waiting for a frame, 1 made, 2 refused (counted once).
    bool scale_reported = false;                   ///< A later scale change was counted already.
    Vec3 made_scale = Vec3(1.0f, 1.0f, 1.0f);      ///< The world scale the collider was made with.
};

/// The `ColliderSource::shape` of a height field (the values below it are `asset::StaticColliderShape`'s).
inline constexpr u32 kColliderShapeHeightField = 4;

/// Whether a source's shape is one that only static bodies carry (a mesh or a height field).
inline bool IsStaticOnlyShape(u32 shape)
{
    return shape == 0 || shape == kColliderShapeHeightField;
}

/// A physics mesh a world made from an `MCOL` at one scale.
///
/// - Thread-safety: plain data.
struct ColliderMeshEntry
{
    asset::ContentHash hash;  ///< The `MCOL`'s content hash.
    u32 scale[3] = {};        ///< The scale's bit patterns (a mirror is a negative z).
    u32 mesh = 0;             ///< Its id in the world's `MeshLibrary`.
    Vec3 extent;              ///< Its `TriangleMesh::Extent()`.
};

/// A physics height field a world made from an `HGHT`.
///
/// - Thread-safety: plain data.
struct ColliderFieldEntry
{
    asset::ContentHash hash;  ///< The `HGHT`'s content hash.
    u32 field = 0;            ///< Its id in the world's `HeightFieldLibrary`.
    Vec3 extent;              ///< Its `HeightField::Extent()`.
    Vec3 centre;              ///< Where the map's centre is in the asset's space (half the map's size, y 0).
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
    /// The world's characters (C API script physics, 2026-11-20): one set for `ENGINE_COMPONENT_CHARACTER` and the
    /// scripts' `character.*`; made with the world, destroyed before its physics world (it holds the world's pre-step
    /// hook).
    UniquePtr<physics::CharacterSet> characters;
    bool physics_used = false;     ///< Whether a body component was set: the frame makes bodies only then.
    Environment environment;       ///< What world_set_environment set (the defaults until then).
    bool environment_set = false;  ///< Whether it was called (no default sun once it was).
    bool audio_used = false;       ///< Whether an audio component was set: the frame places sounds only then.
    /// The world's sounds and the audio host they play on (the share keeps its mixer alive); remade when the host
    /// changes.
    UniquePtr<audio::SpatialAudio> spatial_audio;
    AudioHostPtr spatial_host;
    /// The world's scripts: made by the first `script_load`, script component or host function.
    UniquePtr<ScriptHost> scripts;
    /// The physics meshes made from `MCOL`s; a world keeps them until it is destroyed.
    Vector<ColliderMeshEntry> collider_meshes;
    Vector<ColliderFieldEntry> collider_fields;  ///< The height fields made from `HGHT`s.
    bool collider_sources = false;  ///< Whether a collider source was added: the frame resolves them only then.
    WorldInstances instances;       ///< The instanced sets and trees of the world (the C API's instance_* functions).
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
    u64 max_file_bytes = 0;            ///< `assets.import.max_file_bytes`.
    u64 max_json_bytes = 0;            ///< `assets.import.max_json_bytes`.
    u64 max_buffer_bytes = 0;          ///< `assets.import.max_buffer_bytes`.
    u64 max_output_bytes = 0;          ///< `assets.import.max_output_bytes`.
    u64 max_pixels = 0;                ///< `assets.import.max_pixels`.
    u32 max_nodes = 0;                 ///< `assets.import.max_nodes`.
    u32 max_accessors = 0;             ///< `assets.import.max_accessors`.
    u32 max_images = 0;                ///< `assets.import.max_images`.
    u64 max_collision_triangles = 0;   ///< `assets.import.max_collision_triangles` (glTF colliders).
    f32 light_intensity_scale = 1.0f;  ///< `assets.import.light_intensity_scale`.
    f32 sun_intensity_scale = 1e-4f;   ///< `assets.import.sun_intensity_scale`.
    f32 light_cutoff = 1.0f / 256.0f;  ///< `assets.import.light_cutoff`.
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

    /// The host textures of the engine.
    TextureHost textures;

    /// The font file the labels are drawn with, and how many times it was set.
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

    /// The host-driven audio: made by `audio_open`, destroyed by `audio_close` or with the engine.
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

    /// The surfaces of the engine: made by the first call that needs them.
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
engine_status AnimationSetJointRotationFn(engine_handle world, u64 key, u32 joint, const float* quaternion);
engine_status SkeletonFindJointFn(engine_handle world, u64 key, engine_string name, u32* index);
engine_status SkeletonJointTransformFn(engine_handle world, u64 key, u32 joint, float* matrix);
engine_status EntityAttachToJointFn(engine_handle world, u64 key, u64 skeleton_key, u32 joint);
engine_status LevelSpawnFn(engine_handle world, u64 level, const engine_spawn_params* params, u64* root, u64* keys,
                           size_t capacity, size_t* total);
engine_status PhysicsSetGravityFn(engine_handle world, const float* gravity);
engine_status BodyApplyForceFn(engine_handle world, u64 key, const float* force, const double* point);
engine_status BodyApplyImpulseFn(engine_handle world, u64 key, const float* impulse, const double* point);
engine_status BodySetVelocityFn(engine_handle world, u64 key, const float* linear, const float* angular);
engine_status BodyGetVelocityFn(engine_handle world, u64 key, float* linear, float* angular);
engine_status BodyTeleportFn(engine_handle world, u64 key, const double* position, const float* rotation);
engine_status PhysicsRaycastFn(engine_handle world, const double* origin, const float* direction, float max_distance,
                               u32 layer_mask, engine_ray_hit* hit, int* found);
engine_status PhysicsOverlapSphereFn(engine_handle world, const double* center, float radius, u32 layer_mask, u64* keys,
                                     size_t capacity, size_t* total);
engine_status PhysicsOverlapBoxFn(engine_handle world, const double* center, const float* half_extents, u32 layer_mask,
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
/// See `engine_api.world_set_sky_cube`.
engine_status WorldSetSkyCubeFn(engine_handle world, u64 texture);
/// See `engine_api.terrain_height`.
engine_status TerrainHeightFn(engine_handle world, double x, double z, double* out_height);
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

/// The kind (`ENGINE_SLOT_*`) the material slot `name` takes, or 0 when the material has no such slot.
u32 SlotOverrideKind(StringView name);

/// Sets the override of the material slot `name` on `entity` (the checks of the C call are the caller's: the kind of
/// `value` is the slot's, its numbers are finite, a texture exists). `mesh_slot` is the mesh slot or
/// `SlotOverrides::kAllMeshSlots`; a `mesh_slot_name` is looked up in the entity's mesh instead.
///
/// - Fails: INVALID_ARGUMENT (no mesh reference, a mesh slot above 15), NOT_FOUND (no mesh slot of that name),
///   LIMIT_EXCEEDED (eight overrides held).
engine_status ApplySlotOverrideInWorld(ecs::World& world, ecs::Entity entity, StringView name,
                                       const engine_slot_override& value, u32 mesh_slot,
                                       const StringView* mesh_slot_name, ResourceHost* resources);

/// Removes the override of the slot `name` that applies to exactly `mesh_slot`; `*removed` says whether one was there.
engine_status RemoveSlotOverrideInWorld(ecs::World& world, ecs::Entity entity, StringView name, u32 mesh_slot,
                                        bool* removed);

/// Starts `clip` on `entity` as `animation_play` does (the checks of the parameters are the caller's): pins the clip
/// for the entity's skeleton, cross-fades from the clip playing when `play.fade_seconds` is above 0.
///
/// - Fails: as `ResourceHost::PinClip`, and anything the component write fails with.
engine_status ApplyAnimationPlayInWorld(ecs::World& world, ecs::Entity entity, ResourceHost* resources, u64 clip,
                                        const engine_play_params& play);

/// The world a handle names, or null when the handle is stale or not a world's (the failure text is set as for any
/// handle).
///
/// - Thread-safety: the caller holds `runtime.world_mutex`.
/// - Param status: when not null, receives ENGINE_OK or the failure of the handle.
WorldObject* FindWorld(Runtime& runtime, engine_handle handle, engine_status* status = nullptr);

}  // namespace vse::engine_c
