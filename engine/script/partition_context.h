// The partition context (Phase 27 Day 452): what a binding needs to reach the world, the partition's command buffer
// and the running entity's overlay. A job sets it around the calls it makes into a partition's state; bindings read
// it. It is per thread, so two jobs on two threads never see each other's.
#pragma once

#include "ecs/entity.h"
#include "ecs/world.h"
#include "script/bindings.h"
#include "script/command_buffer.h"
#include "types.h"

namespace engine_placeholder::script
{

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
};

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
