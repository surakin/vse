// The partition context (Phase 27 Day 452): what a binding needs to reach the world, the partition's command buffer
// and the running entity's overlay. A job sets it around the calls it makes into a partition's state; bindings read
// it. It is per thread, so two jobs on two threads never see each other's.
#pragma once

#include "ecs/entity.h"
#include "ecs/world.h"
#include "script/bindings.h"
#include "script/command_buffer.h"
#include "script/log_buffer.h"
#include "types.h"

namespace engine_placeholder::script
{

struct SettingsAccess;
struct SceneServices;

/// What a binding sees of the tick.
///
/// - Thread-safety: thread-compatible; one job owns it.
/// - Ownership: every pointer is borrowed and must outlive the [[script::ScopedPartitionContext]] that publishes it.
struct PartitionContext
{
    /// The world, read-only during the script phase (see [[script::ReadOnlyPhase]]).
    const ecs::World* world = nullptr;
    /// The partition's command buffer.
    CommandBuffer* commands = nullptr;
    /// The running entity's overlay.
    Overlay* overlay = nullptr;
    /// The stable id (`Entity::ToU64`) of the entity whose script is running; the `order_key` of its commands.
    u64 current_entity_key = 0;
    /// The world's random seed (setting `script.random_seed`).
    u32 world_seed = 1;
    /// The tick number the script phase belongs to.
    u64 tick = 0;
    /// The simulation seconds one tick advances.
    f64 dt = 0.0;
    /// The simulation clock in seconds at the tick start (`tick * dt`, kept by the caller).
    f64 sim_time = 0.0;
    /// The frame number (the renderer's frame; may differ from `tick`).
    u64 frame = 0;
    /// Whether a replay is being recorded: `time.real` is refused (it would make the run unrepeatable).
    bool replay_recording = false;
    /// The state of the running entity's random stream; set by [[script::SeedPartitionRandom]], advanced by `random.*`.
    u64 random_state = 0;
    /// The partition's log buffer; null sends `log.*` straight to the log.
    LogBuffer* log = nullptr;
    /// The partition's view of the settings (snapshot, policy, subscriptions); null when scripts have no settings.
    SettingsAccess* settings = nullptr;
    /// The scene's names and prefabs (see [[script::SceneServices]]); null when scripts see none.
    const SceneServices* scene = nullptr;
};

/// The first state of an entity's random stream this tick: a fixed 64-bit mix of the world seed, the entity's stable id
/// and the tick. It depends on nothing else (not the thread, the partition count or the order entities run in), so
/// the stream is the same for any thread count.
///
/// - Thread-safety: thread-safe.
/// - Fails: never.
u64 RandomSeedFor(u32 world_seed, u64 entity_key, u64 tick);

/// The next 64 random bits of a stream (SplitMix64: integer arithmetic only, so the same on every machine).
///
/// - Param state: the stream's state, advanced; not null.
/// - Ownership: borrowed for the call.
/// - Thread-safety: thread-compatible.
/// - Fails: never.
u64 AdvanceRandom(u64* state);

/// Starts the random stream of the entity `context->current_entity_key` for `context->tick`: call it once per entity
/// per tick, after setting both (a second call repeats the same numbers).
///
/// - Param context: the context to seed; not null.
/// - Ownership: borrowed for the call.
/// - Thread-safety: thread-compatible.
/// - Fails: never.
void SeedPartitionRandom(PartitionContext* context);

/// Publishes a [[script::PartitionContext]] for the current thread while it lives; restores the previous one after.
///
/// - Thread-safety: thread-compatible; use it on one thread.
/// - Ownership: borrows the context.
class ScopedPartitionContext
{
public:
    /// Publishes `context` for this thread.
    ///
    /// - Ownership: borrows the context, which must outlive this object.
    explicit ScopedPartitionContext(PartitionContext* context);
    /// Restores the previous context.
    ~ScopedPartitionContext();
    ScopedPartitionContext(const ScopedPartitionContext&) = delete;
    ScopedPartitionContext& operator=(const ScopedPartitionContext&) = delete;

private:
    PartitionContext* previous_;
};

/// The context published for this thread.
///
/// - Returns: the context, or null outside a partition job.
/// - Ownership: borrowed.
/// - Thread-safety: thread-safe (it reads this thread's own slot).
/// - Fails: null, as described.
PartitionContext* CurrentPartitionContext();

/// An [[script::EntityCheck]] that asks the current thread's context: an entity is alive when the world holds it, or
/// when it is a placeholder the partition's buffer reserved this tick (so a script can use a handle it spawned
/// before the merge). Entities destroyed earlier in the same tick still count as alive: the view is the tick start.
///
/// - Returns: the check; it has no `user` pointer and is safe to share between partitions.
/// - Fails: never.
EntityCheck ContextEntityCheck();

/// Holds a world read-only for the script phase: structural changes abort until it ends (the ECS parallel-phase
/// guard), so every partition reads the same tick-start values.
///
/// - Thread-safety: thread-compatible; begin and end it on the thread that runs the phase, with every job finished
/// before the end.
/// - Ownership: borrows the world.
class ReadOnlyPhase
{
public:
    /// Begins the phase.
    ///
    /// - Ownership: borrows the world, which must outlive this object.
    explicit ReadOnlyPhase(ecs::World* world) : world_(world) { world_->BeginParallel(); }
    /// Ends the phase.
    ~ReadOnlyPhase() { world_->EndParallel(); }
    ReadOnlyPhase(const ReadOnlyPhase&) = delete;
    ReadOnlyPhase& operator=(const ReadOnlyPhase&) = delete;

private:
    ecs::World* world_;
};

}  // namespace engine_placeholder::script
