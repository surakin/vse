// The ordered merge (Phase 27 Day 452): the sync point where the commands of every partition's buffer are applied to
// the world in one total order, `(partition, entity stable id, sequence)`, whatever the thread count and timing were.
#pragma once

#include "containers/hash_map.h"
#include "containers/span.h"
#include "ecs/entity.h"
#include "ecs/world.h"
#include "mem/heap_allocator.h"
#include "script/command_buffer.h"
#include "types.h"

namespace vse::script
{

/// What the merge does when two partitions change the same thing of one entity in one tick.
///
/// - Thread-safety: thread-compatible plain data.
enum class ConflictPolicy : u8
{
    /// The command applied last (the higher partition) wins; the conflict is only counted.
    LastWins,
    /// The first command stays; the later conflicting command is refused and counted as an error.
    Error,
};

/// The state of one merge, passed to every apply function.
///
/// - Thread-safety: thread-compatible; the merge runs on one thread.
/// - Ownership: owns the placeholder map of the merge.
class MergeContext
{
public:
    /// Creates a context for `world`.
    ///
    /// - Param world: the world the commands change; borrowed.
    /// - Param user: passed through to apply functions.
    /// - Ownership: borrows both; they must outlive the context.
    explicit MergeContext(ecs::World* world, void* user = nullptr)
        : world_(world), user_(user), bound_(HeapAllocator::Default())
    {
    }

    /// The world being changed.
    ///
    /// - Ownership: borrowed.
    /// - Fails: never.
    ecs::World* World() const { return world_; }
    /// The pointer given at construction.
    ///
    /// - Ownership: borrowed.
    /// - Fails: never.
    void* User() const { return user_; }
    /// The partition whose command is being applied.
    ///
    /// - Fails: never.
    u32 Partition() const { return partition_; }

    /// Says that the entity made for `placeholder` is `real`; later commands that name the placeholder change `real`.
    /// A create command calls it.
    ///
    /// - Fails: never.
    void Bind(ecs::Entity placeholder, ecs::Entity real) { bound_.Insert(placeholder, real); }

    /// The real entity for a handle: the entity made for a placeholder, or `entity` itself when it is not one.
    ///
    /// - Returns: the invalid entity for a placeholder that has not been bound.
    /// - Fails: returns the invalid entity, as described.
    ecs::Entity Resolve(ecs::Entity entity) const
    {
        if (!entity.IsPlaceholder())
        {
            return entity;
        }
        const ecs::Entity* found = bound_.Find(entity);
        return found != nullptr ? *found : ecs::Entity();
    }

private:
    friend struct MergeAccess;
    ecs::World* world_;
    void* user_;
    u32 partition_ = 0;
    HashMap<ecs::Entity, ecs::Entity, HeapAllocator> bound_;
};

/// Applies one command: `target` is the resolved entity (the placeholder itself for a creating kind); returns whether
/// the command changed the world (false counts as skipped: a dead entity, a missing component).
using ApplyFunction = bool (*)(MergeContext& context, ecs::Entity target, Span<const u8> payload);

/// How the merge treats one command kind.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: `name` is a literal.
struct CommandKind
{
    /// The kind's name, for logs and reports.
    const char* name = nullptr;
    /// Applies a command of the kind; null means the id is not registered.
    ApplyFunction apply = nullptr;
    /// A creating kind: its target is a placeholder that `apply` binds to the new entity.
    bool creates = false;
    /// A kind that overwrites state of its target: two partitions using it on one entity in one tick conflict.
    bool writes = false;
    /// Whether the command is not about an entity: its target is ignored (it may be the null entity), and two
    /// partitions issuing it is not a conflict (the later one applies, as in a sequence).
    bool global = false;
};

/// The kinds a world's commands may have, by id.
///
/// - Thread-safety: thread-compatible: fill it, then share it read-only.
/// - Ownership: owns the entries.
class CommandKindTable
{
public:
    /// The most kinds.
    static constexpr usize kMaxKinds = 256;

    /// Registers a kind.
    ///
    /// - Param id: below `kMaxKinds`.
    /// - Param kind: copied; `apply` and `name` must not be null.
    /// - Returns: false when the id is out of range or already used.
    /// - Fails: returns false, as described.
    bool Register(u16 id, const CommandKind& kind)
    {
        if (id >= kMaxKinds || kind.apply == nullptr || kind.name == nullptr || kinds_[id].apply != nullptr)
        {
            return false;
        }
        kinds_[id] = kind;
        return true;
    }

    /// Finds a kind.
    ///
    /// - Returns: the kind, or null when the id is not registered.
    /// - Ownership: borrowed from the table.
    /// - Fails: null, as described.
    const CommandKind* Find(u16 id) const
    {
        return id < kMaxKinds && kinds_[id].apply != nullptr ? &kinds_[id] : nullptr;
    }

private:
    CommandKind kinds_[kMaxKinds];
};

/// What a merge did.
///
/// - Thread-safety: thread-compatible plain data.
struct MergeReport
{
    /// Commands that changed the world.
    u64 applied = 0;
    /// Commands whose apply function returned false (a dead entity, a missing component).
    u64 skipped = 0;
    /// Commands naming a placeholder that was never created.
    u64 dangling = 0;
    /// Commands whose kind is not registered.
    u64 unknown = 0;
    /// Pairs of commands from different partitions that changed the same entity's `writes` kind.
    u64 conflicts = 0;
    /// Commands refused by [[script::ConflictPolicy::Error]].
    u64 refused = 0;
};

/// Applies the commands of every buffer to the world in the order `(partition, order_key, sequence)`.
///
/// A buffer that is not already sorted is sorted first. The order, and so the resulting world, does not depend on the
/// number of threads that filled the buffers or on how they were timed. The world must not be in a script phase.
///
/// - Param world: the world to change; no iteration or parallel phase active.
/// - Param buffers: one buffer per partition, in strictly increasing partition order.
/// - Param kinds: the registered kinds.
/// - Param policy: what to do on a conflict.
/// - Param user: passed to apply functions through the [[script::MergeContext]].
/// - Returns: the counts.
/// - Ownership: the buffers are sorted when needed and otherwise left as they were; clear them with `Begin`.
/// - Aborts: the buffers are not in increasing partition order (debug checks).
/// - Fails: reports in the counts, as described.
MergeReport Merge(ecs::World* world, Span<CommandBuffer* const> buffers, const CommandKindTable& kinds,
                  ConflictPolicy policy, void* user = nullptr);

}  // namespace vse::script
