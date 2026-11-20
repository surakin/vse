// The per-partition command buffer and the own-entity overlay (Phase 27 Day 452). While scripts run, the world is
// read-only; everything a script changes is recorded here and applied at the sync point by [[script::Merge]]
// (docs/guide/scripting.md, "The world view and the merge").
#pragma once

#include "containers/span.h"
#include "containers/hash_map.h"
#include "int128.h"
#include "containers/vector.h"
#include "ecs/entity.h"
#include "lang/traits.h"
#include "script/value.h"
#include "types.h"

namespace vse::script
{

/// How many entity handles one partition can reserve per tick (the placeholders a script may use before the merge).
inline constexpr u32 kPlaceholdersPerPartition = u32{1} << 22;

/// One recorded command.
///
/// - Thread-safety: thread-compatible plain data.
struct CommandRecord
{
    /// The stable id (`Entity::ToU64`) of the entity whose script recorded the command: the second sort key.
    u64 order_key = 0;
    /// The position in the buffer: the third sort key.
    u32 sequence = 0;
    /// The kind, registered in a [[script::CommandKindTable]].
    u16 kind = 0;
    /// The entity the command changes: a real entity or a placeholder from [[script::CommandBuffer::Reserve]].
    ecs::Entity target;
    /// Where the payload starts in the buffer's bytes.
    u32 payload_offset = 0;
    /// The payload size in bytes.
    u32 payload_size = 0;
};

/// The commands one partition recorded during a script phase.
///
/// Commands are numbered as they are recorded. A partition runs its entities in stable id order, so its buffer is
/// already sorted by `(order_key, sequence)`; the merge checks that and sorts a buffer that is not, so the result never
/// depends on the order a partition happened to run its entities in. Payloads are plain bytes copied into one growing
/// block; the storage is kept between ticks.
///
/// - Thread-safety: thread-compatible: one writer (the job that holds the partition); the merge reads it afterwards.
/// - Ownership: owns the commands and their bytes.
class CommandBuffer
{
public:
    /// Creates an empty buffer for no partition.
    CommandBuffer() = default;

    /// Starts a tick for a partition: forgets the old commands and placeholders, keeps the storage.
    ///
    /// - Param partition: the partition number below `kMaxPartitions`; part of every placeholder this buffer reserves.
    /// - Aborts: `partition` is not below `kMaxPartitions`.
    /// - Fails: never.
    void Begin(u32 partition);

    /// The partition number given to `Begin`.
    ///
    /// - Fails: never.
    u32 Partition() const { return partition_; }

    /// Records a command.
    ///
    /// - Param kind: the command kind.
    /// - Param order_key: the stable id of the entity whose script records it.
    /// - Param target: the entity to change (a real one, or a placeholder of this buffer).
    /// - Param payload: `size` bytes copied into the buffer; may be null when `size` is 0.
    /// - Param size: the payload size.
    /// - Returns: true when recorded; false when the buffer is full (16 Mi commands or 1 GiB of payload) or `size`
    /// does not fit 32 bits, or `payload` is null while `size` is not 0. The caller reports the failure to the script.
    /// - Fails: returns false, as described.
    bool Record(u16 kind, u64 order_key, ecs::Entity target, const void* payload, u32 size);

    /// Records a command whose payload is one value of a trivially copyable type.
    ///
    /// - Fails: returns false, as for `Record`.
    template <class T> bool Record(u16 kind, u64 order_key, ecs::Entity target, const T& payload)
    {
        static_assert(IsTriviallyCopyable<T>, "a command payload is plain bytes");
        return Record(kind, order_key, target, &payload, static_cast<u32>(sizeof(T)));
    }

    /// Reserves an entity handle that commands of this buffer may name before the merge creates the entity.
    ///
    /// Reservations count up from 0 per tick, so the handle depends only on the partition and the script's own order
    /// of calls, never on the thread count. The handle is a placeholder: `generation` is
    /// `Entity::kPlaceholderGeneration` and `index` is `partition * kPlaceholdersPerPartition + n`.
    ///
    /// - Returns: the placeholder, or the invalid entity when the partition's block is used up.
    /// - Fails: returns the invalid entity, as described.
    ecs::Entity Reserve();

    /// Whether `entity` is a placeholder this buffer reserved this tick.
    ///
    /// - Fails: never.
    bool IsReserved(ecs::Entity entity) const;

    /// The number of commands.
    ///
    /// - Fails: never.
    usize Size() const { return commands_.Size(); }

    /// The commands in recording order (or sorted order after `SortIfNeeded`).
    ///
    /// - Ownership: borrowed; valid until the next `Begin` or `Record`.
    /// - Fails: never.
    Span<const CommandRecord> Commands() const { return Span<const CommandRecord>(commands_.Data(), commands_.Size()); }

    /// The payload of a command.
    ///
    /// - Ownership: borrowed; valid until the next `Begin` or `Record`.
    /// - Fails: never.
    Span<const u8> PayloadOf(const CommandRecord& record) const
    {
        return Span<const u8>(bytes_.Data() + record.payload_offset, record.payload_size);
    }

    /// Whether the commands are in `(order_key, sequence)` order.
    ///
    /// - Fails: never.
    bool IsSorted() const;

    /// Sorts the commands by `(order_key, sequence)` when they are not in that order already (stable).
    ///
    /// - Fails: never.
    void SortIfNeeded();

    /// The bytes of payload held.
    ///
    /// - Fails: never.
    usize PayloadBytes() const { return bytes_.Size(); }

private:
    Vector<CommandRecord> commands_;
    Vector<u8> bytes_;
    u32 partition_ = 0;
    u32 reserved_ = 0;
};

/// What the running entity has written this tick, so its own reads see its own writes.
///
/// A script reads the live world, which is read-only during the phase and so holds the tick-start values; a read of
/// something the same entity's script already wrote this tick is answered from here instead. The driver clears it
/// before each entity's script runs, so only the running entity's writes are in it.
///
/// - Thread-safety: thread-compatible: owned by one partition job.
/// - Ownership: owns the values.
class Overlay
{
public:
    /// Forgets everything.
    ///
    /// - Fails: never.
    void Clear() { entries_.Clear(); }

    /// Remembers that `field` of `entity` now has `value` (a later `Put` of the same pair replaces it).
    ///
    /// - Param field: a number the binding layer chooses per readable field.
    /// - Fails: never.
    void Put(ecs::Entity entity, u32 field, Value value);

    /// The value written for the pair this tick.
    ///
    /// - Returns: the value, or null when the entity's script has not written the field.
    /// - Ownership: borrowed; valid until the next `Put` or `Clear`.
    /// - Fails: null, as described.
    const Value* Find(ecs::Entity entity, u32 field) const;

    /// The number of entries.
    ///
    /// - Fails: never.
    usize Size() const { return entries_.Size(); }

private:
    // (entity, field) as one exact key: the entity's 64 bits above the field's 32 (container audit Day 9: a scan per
    // access). Nothing walks it, so its order does not matter.
    static u128 KeyOf(ecs::Entity entity, u32 field) { return (u128(entity.ToU64()) << 32) | u128(field); }
    HashMap<u128, Value, HeapAllocator> entries_{HeapAllocator::Default()};
};

}  // namespace vse::script
