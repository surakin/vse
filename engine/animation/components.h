#pragma once

//! The animation components and the system that plays them.

#include "animation/pose.h"
#include "animation/sampler.h"
#include "asset/clip.h"
#include "asset/resource_id.h"
#include "asset/skeleton.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "ecs/entity.h"
#include "ecs/world.h"
#include "jobs/scheduler.h"
#include "storage/component_registry.h"
#include "types.h"

namespace vse::animation
{

/// Stable ids of the animation components in the storage registry (the engine's own range, below 1000).
enum ComponentIds : u32
{
    kAnimationPlayerId = 40,   ///< [[animation::AnimationPlayer]]: stored.
    kSkeletonInstanceId = 41,  ///< [[animation::SkeletonInstance]]: stored (the skeleton id only).
    kRootMotionId = 42,        ///< [[animation::RootMotion]]: derived.
    kMorphWeightsId = 43,      ///< [[animation::MorphWeights]]: stored.
    kJointRotationsId = 44,    ///< [[animation::JointRotations]]: derived (a host sets it each run).
};

/// Most morph weights of one entity: the mesh blob's limit on morph targets.
inline constexpr u32 kMaxMorphWeights = 64;

/// The weights of the entity's mesh's morph targets.
///
/// Weight `i` is target `i`'s, the first `count` of them; the rest are 0. Set by levels (glTF's `mesh.weights` and
/// `node.weights`), saves, scripts, and the animation system when the entity plays a clip with a weight track;
/// `scene::MakeRenderSyncSystem` hands them to the render objects.
///
/// - Thread-safety: thread-compatible plain data; the system writes it from a job that owns the entity's chunk.
struct MorphWeights
{
    /// How many of `weights` are used; at most [[animation::kMaxMorphWeights]] (the render sync clamps a larger count
    /// and counts it as invalid).
    u32 count = 0;
    /// The weights, from target 0.
    Array<f32, kMaxMorphWeights> weights = {};
};

/// Plays one clip on an entity: the clip, the time and how it advances. Stored with its entity.
///
/// - Thread-safety: thread-compatible plain data; the system writes it from a job that owns the entity's chunk.
struct AnimationPlayer
{
    u64 clip = 0;           ///< The clip's resource id (`asset::ResourceId::Value()`); 0 plays nothing.
    f32 time = 0;           ///< The play position in seconds.
    f32 speed = 1.0f;       ///< Seconds of clip per second of frame time; a negative speed counts as 0.
    f32 weight = 1.0f;      ///< The blend weight of the sampled pose against the bind pose, in [0, 1].
    bool playing = true;    ///< Whether time advances.
    bool looping = true;    ///< Whether time wraps at the end.
    bool finished = false;  ///< Set when a clip that does not loop reached its end (runtime state, not stored).
    bool started = false;   ///< Runtime state, not stored: false until the first update, which then includes events at
                            ///< the starting time.
    f32 step_from = 0;      ///< Set by the system; the time at the start of the last update.
    u32 step_wraps = 0;     ///< Set by the system; the loops the last update passed.
    f32 step_time = 0;      ///< Set by the system; the time at the end of the last update.
    bool step_include = false;  ///< Set by the system; the last update included events at its start.
    bool step_valid = false;  ///< Set by the system; the last update advanced the clip (events are read from the step).
    SamplerState sampler;     ///< Set by the system; the cursors.
    u64 fade_clip = 0;        ///< The clip being faded out (a cross-fade to `clip`); 0 when there is none. Not stored.
    f32 fade_time = 0;        ///< The play position of the fading clip, in seconds; the system advances it.
    f32 fade_total = 0;       ///< The cross-fade's length in seconds; the fade ends when `fade_elapsed` reaches it.
    f32 fade_elapsed = 0;     ///< Seconds of the cross-fade done; the system advances it.
    SamplerState fade_sampler;  ///< Set by the system; the cursors of the fading clip.
};

/// The skeleton an entity is drawn with and the skinning palette the system fills each update.
///
/// - Thread-safety: thread-compatible; the system writes it from a job that owns the entity's chunk.
struct SkeletonInstance
{
    u64 skeleton = 0;        ///< The skeleton's resource id (`asset::ResourceId::Value()`).
    Vector<Mat3x4> palette;  ///< Set by the system; one 48-byte matrix per joint, the GPU palette layout.
};

/// One rotation a host adds to a joint.
///
/// - Thread-safety: thread-compatible plain data.
struct JointRotation
{
    u32 joint = 0;                                        ///< The joint's index in the skeleton.
    Array<f32, 4> rotation = {{0.0f, 0.0f, 0.0f, 1.0f}};  ///< A unit quaternion (x, y, z, w), never the identity.
};

/// Rotations a host adds to joints of a playing skeleton (a head that follows where the player looks), kept in order
/// of joint, at most one per joint.
///
/// After the entity's clips are sampled and blended, each joint's local rotation becomes the pose's rotation composed
/// with this one (`pose * added`, so the rotation is about the joint's own axes whatever the clip does). The entity
/// need not play a clip: the bind pose gets the rotations. Present on entities that have a
/// [[animation::SkeletonInstance]]; derived (not saved).
///
/// - Thread-safety: thread-compatible; the system reads it from a job that owns the entity's chunk.
struct JointRotations
{
    Vector<JointRotation> list;  ///< Ordered by joint.
};

/// What [[animation::CheckHostRotation]] found.
enum class HostRotationCheck : u8
{
    Ok,         ///< A usable rotation.
    NotFinite,  ///< A component is NaN or infinite.
    NotUnit,    ///< The length is not 1 within 0.1%.
};

/// Checks a rotation a host gives for a joint and makes it unit length.
///
/// - Param in: x, y, z, w.
/// - Param normalized: set to `in` scaled to length 1 when the result is `Ok`.
/// - Returns: whether `in` is finite and of length 1 within 0.1%.
/// - Fails: returns the reason, as described.
/// - Ownership: borrows its arguments for the call.
HostRotationCheck CheckHostRotation(const Array<f32, 4>& in, Array<f32, 4>* normalized);

/// Whether a unit rotation is the identity (either sign of w), which takes a joint's host rotation away.
///
/// - Fails: never.
bool IsIdentityRotation(const Array<f32, 4>& rotation);

/// Sets the host rotation of `joint` in the list, keeping it ordered by joint; the identity removes the joint's entry.
///
/// - Param rotation: a unit rotation, as [[animation::CheckHostRotation]] leaves it.
/// - Fails: never.
/// - Ownership: borrows its arguments for the call.
void SetHostJointRotation(JointRotations* rotations, u32 joint, const Array<f32, 4>& rotation);

/// The root joint's movement over the last update, for a clip flagged root motion: the game applies it. Present only
/// when the entity has the component.
///
/// - Thread-safety: thread-compatible plain data.
struct RootMotion
{
    RootMotionDelta delta;  ///< Over the last update.
};

/// One event crossed during an update.
///
/// - Thread-safety: thread-compatible plain data.
struct AnimationEvent
{
    ecs::Entity entity;  ///< The entity whose player crossed it.
    f32 time = 0;        ///< The event's time in its clip.
    u32 name_hash = 0;   ///< `asset::JointNameHash` of the event's name.
    u32 payload = 0;     ///< The event's payload.
};

/// The events crossed by the last [[animation::AnimationSystem::Update]], in entity order and, per entity, time order.
/// A world resource; the system clears and refills it each update when it exists.
///
/// - Thread-safety: thread-compatible.
struct AnimationEvents
{
    Vector<AnimationEvent> events;  ///< The events.
};

/// Finds the views an update needs. Implemented by the resource manager's animation adapter or a test; called
/// from several jobs at once, so it must be safe to read concurrently.
///
/// - Thread-safety: thread-safe for concurrent calls.
class AnimationResolver
{
public:
    virtual ~AnimationResolver() = default;
    /// The skeleton with this id, or null when it is not loaded.
    ///
    /// - Fails: never.
    /// - Ownership: the view belongs to the resolver and stays valid while it is loaded.
    virtual const asset::SkeletonView* FindSkeleton(asset::ResourceId id) const = 0;
    /// The clip with this id, or null when it is not loaded.
    ///
    /// - Fails: never.
    /// - Ownership: the view belongs to the resolver and stays valid while it is loaded.
    virtual const asset::ClipView* FindClip(asset::ResourceId id) const = 0;
};

/// What one update did.
///
/// - Thread-safety: thread-compatible plain data.
struct AnimationStats
{
    u64 players_updated = 0;  ///< Entities sampled.
    u64 refused = 0;          ///< Entities skipped: unknown skeleton or clip, or a clip made for another skeleton.
    u64 events = 0;           ///< Events published.
    u64 events_dropped = 0;   ///< Events that did not fit one update's per-entity capacity.
};

/// Registers the animation components with the storage registry so levels save them.
///
/// - Fails: an id or name already registered.
/// - Ownership: borrows its arguments for the call.
storage::Status RegisterAnimationComponents(storage::ComponentRegistry* registry);

/// Advances and samples every entity that has an [[animation::AnimationPlayer]] and a [[animation::SkeletonInstance]],
/// in parallel over the chunks (an additive clip is refused: it is a difference to layer on a pose, not a pose), then
/// publishes the events in entity order. Each entity's result depends only on its own
/// components, so the palettes are the same at any worker count.
///
/// - Thread-safety: thread-compatible; one update at a time.
class AnimationSystem
{
public:
    /// Makes a system that resolves resources through `resolver`.
    ///
    /// - Ownership: borrows `resolver` for the system's life.
    explicit AnimationSystem(const AnimationResolver* resolver) : resolver_(resolver) {}

    /// Runs one update of `dt` seconds. An entity whose skeleton or clip the resolver does not know, or whose clip was
    /// made for another skeleton (id, hash or joint count), is skipped and counted in `refused`, with one log line the
    /// first time per entity.
    ///
    /// - Param scheduler: the job scheduler, or null to run on the calling thread.
    /// - Fails: never.
    /// - Aborts: `world` is being iterated.
    /// - Ownership: borrows its arguments for the call.
    AnimationStats Update(ecs::World* world, jobs::Scheduler* scheduler, f32 dt);

private:
    const AnimationResolver* resolver_ = nullptr;
};

}  // namespace vse::animation
