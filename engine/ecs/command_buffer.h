#pragma once

//! Deferred structural changes: record creates, destroys, adds, removes and sets while a query runs, apply them at a
//! sync point.

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/component.h"
#include "ecs/entity.h"
#include "ecs/index_list.h"
#include "ecs/world.h"
#include "lang/new.h"
#include "lang/traits.h"
#include "lang/utility.h"
#include "log/assert.h"
#include "mem/heap_allocator.h"
#include "types.h"

namespace vse::ecs
{

namespace detail
{

/// Where each value of a `Create<Ts...>` pack sits in its payload, and the payload's size and alignment.
template <class... Ts> struct PackLayout
{
    static constexpr usize kCount = sizeof...(Ts);
    usize offset[kCount == 0 ? 1 : kCount] = {};
    usize size = 0;
    usize align = 1;
};

template <class... Ts> constexpr PackLayout<Ts...> ComputePackLayout()
{
    PackLayout<Ts...> layout;
    const usize sizes[] = {sizeof(Ts)..., 0};
    const usize aligns[] = {alignof(Ts)..., 1};
    usize at = 0;
    for (usize i = 0; i < sizeof...(Ts); ++i)
    {
        at = (at + aligns[i] - 1) / aligns[i] * aligns[i];
        layout.offset[i] = at;
        at += sizes[i];
        if (aligns[i] > layout.align)
        {
            layout.align = aligns[i];
        }
    }
    layout.size = at;
    return layout;
}

}  // namespace detail

/// A recording of structural changes to apply later, so a system can create, destroy and re-shape entities while a
/// query is running.
///
/// Record with `Create`, `Destroy`, `Add`, `Remove` and `Set`; apply with [[ecs::World::Apply]] at a sync point.
/// `Create` returns a
/// **placeholder** entity (`Entity::IsPlaceholder()`): later commands in the same buffer may name it, and
/// [[ecs::CommandBuffer::Resolve]] gives the real entity after the buffer was applied. Component values are moved into
/// the buffer when recorded and moved out when applied; a command that is never applied destroys its values when the
/// buffer is cleared or destroyed.
///
/// The buffer keeps its memory between uses (`Clear` releases no storage), so a system that records every frame
/// allocates only on the first frames. A buffer is not tied to a world: it holds no world pointer.
///
/// - Thread-safety: thread-compatible. One writer at a time; give each parallel job its own buffer
/// ([[ecs::CommandBufferPool]]).
/// - Ownership: owns the recorded values until they are applied or cleared. A placeholder is valid only with the buffer
/// that made it.
class CommandBuffer
{
public:
    /// The number of bytes of one payload block; one command's values must fit in a block (a row can never be larger
    /// than a chunk, so they do).
    static constexpr usize kBlockBytes = usize{64} * 1024;
    /// The largest alignment a recorded value may have (blocks are aligned to this).
    static constexpr usize kMaxAlign = 64;

    /// Makes an empty buffer.
    CommandBuffer() = default;
    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;
    /// Destroys the values of commands that were not applied and frees the storage.
    ~CommandBuffer();

    /// Records the creation of an entity with no components.
    /// - Returns: the placeholder entity.
    /// - Fails: never.
    Entity Create();
    /// Records the creation of an entity with the given components (moved into the buffer).
    ///
    /// - Returns: the placeholder entity, or the invalid entity if the values do not fit one payload block (counted in
    /// `Refused()`).
    /// - Aborts: a component type is listed twice (checked when applied, by `World::Create`).
    template <class... Ts> Entity Create(Ts... values);
    /// Records the destruction of an entity or a placeholder.
    /// - Fails: never; a dead entity is skipped when applied.
    void Destroy(Entity entity);
    /// Records adding component `T` to an entity or placeholder.
    ///
    /// - Param value: moved into the buffer.
    /// - Fails: a value that does not fit a payload block is dropped and counted in `Refused()`.
    template <class T> void Add(Entity entity, T value);
    /// Records removing component `T` from an entity or placeholder.
    /// - Fails: never; a missing component is skipped when applied.
    template <class T> void Remove(Entity entity);
    /// Records setting component `T` (an overwrite, or an add if absent).
    ///
    /// - Param value: moved into the buffer.
    /// - Fails: as for `Add`.
    template <class T> void Set(Entity entity, T value);

    /// The real entity a placeholder became when the buffer was applied.
    ///
    /// - Param entity: a placeholder of this buffer, or any entity.
    /// - Returns: the created entity; `entity` itself when it is not a placeholder; the invalid entity when the
    /// placeholder's `Create` has not been applied
    ///   yet, was skipped, or the placeholder is not from this buffer.
    /// - Fails: returns the invalid entity as described.
    Entity Resolve(Entity entity) const;

    /// The number of commands recorded and not yet applied.
    /// - Fails: never.
    usize Size() const { return commands_.Size(); }
    /// Returns whether no command is waiting to be applied.
    /// - Fails: never.
    bool IsEmpty() const { return commands_.Size() == 0; }
    /// The number of commands refused because their values did not fit a payload block.
    /// - Fails: never.
    u64 Refused() const { return refused_; }
    /// The bytes of payload storage in use plus reserved (it only grows).
    /// - Fails: never.
    usize BytesReserved() const { return blocks_.Size() * kBlockBytes; }

    /// Drops the commands that were not applied (destroying their values) and forgets the placeholders; keeps the
    /// storage.
    /// - Fails: never.
    void Clear();

private:
    friend class World;

    struct Context
    {
        World* world;
        CommandBuffer* buffer;
    };
    struct Command
    {
        Status (*apply)(Context&, const Command&, byte*) = nullptr;
        void (*drop)(byte*) = nullptr;
        Entity target;
        u32 block = 0;
        u32 offset = 0;
        bool has_payload = false;
        bool creates = false;
    };

    // Applies every waiting command in order, then forgets them (the placeholders stay resolvable until Clear).
    PlaybackReport Play(World& world);

    // Reserves `size` bytes aligned to `align` in a block that never moves; returns null if they cannot fit.
    byte* Reserve(usize size, usize align, u32* block, u32* offset);
    byte* PayloadOf(const Command& command) const
    {
        return command.has_payload ? blocks_[command.block] + command.offset : nullptr;
    }
    Entity NewPlaceholder();

    template <class T> static Status ApplyAdd(Context& context, const Command& command, byte* payload);
    template <class T> static Status ApplySet(Context& context, const Command& command, byte* payload);
    template <class T> static Status ApplyRemove(Context& context, const Command& command, byte* payload);
    template <class... Ts, usize... I>
    static Status ApplyCreate(Context& context, const Command& command, byte* payload, detail::IndexList<I...>);
    template <class... Ts, usize... I> static void DropPack(byte* payload, detail::IndexList<I...>);
    template <class... Ts, usize... I> static void MoveInto(byte* payload, detail::IndexList<I...>, Ts&... values);
    static Status ApplyDestroy(Context& context, const Command& command, byte* payload);

    Vector<Command> commands_;
    Vector<byte*> blocks_;
    u32 current_block_ = 0;
    usize used_ = 0;
    Vector<Entity> created_;
    u64 refused_ = 0;
};

/// One command buffer per worker, so parallel jobs record without sharing, applied in a fixed order afterwards.
///
/// - Thread-safety: thread-compatible. Distinct buffers may be written by different threads at once; `ApplyAll` and
/// `ClearAll` are exclusive.
/// - Ownership: owns its buffers.
class CommandBufferPool
{
public:
    /// Makes `count` empty buffers.
    /// - Param count: how many buffers (at least 1).
    explicit CommandBufferPool(u32 count);
    /// Grows the pool to at least `count` buffers (existing buffers and their contents are kept).
    /// - Fails: never.
    void EnsureCount(u32 count);
    /// The number of buffers.
    /// - Fails: never.
    u32 Count() const { return static_cast<u32>(buffers_.Size()); }
    /// The buffer with number `index` (a worker's number).
    /// - Aborts: `index` is not below `Count()`.
    CommandBuffer& At(u32 index);
    /// Applies the buffers to `world` in index order.
    /// - Returns: the reports added together.
    /// - Aborts: the world is being iterated.
    PlaybackReport ApplyAll(World& world);
    /// Clears every buffer; keeps their storage.
    /// - Fails: never.
    void ClearAll();

private:
    Vector<UniquePtr<CommandBuffer>> buffers_;
};

template <class... Ts> Entity CommandBuffer::Create(Ts... values)
{
    if constexpr (sizeof...(Ts) == 0)
    {
        return Create();
    }
    else
    {
        constexpr detail::PackLayout<Ts...> layout = detail::ComputePackLayout<Ts...>();
        static_assert(layout.align <= kMaxAlign,
                      "CommandBuffer::Create: a component is aligned beyond CommandBuffer::kMaxAlign");
        u32 block = 0;
        u32 offset = 0;
        byte* payload = Reserve(layout.size, layout.align, &block, &offset);
        if (payload == nullptr)
        {
            ++refused_;
            return Entity{};
        }
        using Indices = typename detail::MakeIndexList<sizeof...(Ts)>::Type;
        MoveInto<Ts...>(payload, Indices{}, values...);
        Command command;
        command.apply = [](Context& c, const Command& cmd, byte* p)
        { return ApplyCreate<Ts...>(c, cmd, p, Indices{}); };
        command.drop = [](byte* p) { DropPack<Ts...>(p, Indices{}); };
        command.block = block;
        command.offset = offset;
        command.has_payload = true;
        command.creates = true;
        command.target = NewPlaceholder();
        commands_.Push(command);
        return command.target;
    }
}

template <class T> void CommandBuffer::Add(Entity entity, T value)
{
    static_assert(alignof(T) <= kMaxAlign,
                  "CommandBuffer::Add: the component is aligned beyond CommandBuffer::kMaxAlign");
    u32 block = 0;
    u32 offset = 0;
    byte* payload = Reserve(sizeof(T), alignof(T), &block, &offset);
    if (payload == nullptr)
    {
        ++refused_;
        return;
    }
    ConstructAt<T>(payload, Move(value));
    Command command;
    command.apply = &ApplyAdd<T>;
    command.drop = [](byte* p) { reinterpret_cast<T*>(p)->~T(); };
    command.target = entity;
    command.block = block;
    command.offset = offset;
    command.has_payload = true;
    commands_.Push(command);
}

template <class T> void CommandBuffer::Set(Entity entity, T value)
{
    static_assert(alignof(T) <= kMaxAlign,
                  "CommandBuffer::Set: the component is aligned beyond CommandBuffer::kMaxAlign");
    u32 block = 0;
    u32 offset = 0;
    byte* payload = Reserve(sizeof(T), alignof(T), &block, &offset);
    if (payload == nullptr)
    {
        ++refused_;
        return;
    }
    ConstructAt<T>(payload, Move(value));
    Command command;
    command.apply = &ApplySet<T>;
    command.drop = [](byte* p) { reinterpret_cast<T*>(p)->~T(); };
    command.target = entity;
    command.block = block;
    command.offset = offset;
    command.has_payload = true;
    commands_.Push(command);
}

template <class T> void CommandBuffer::Remove(Entity entity)
{
    Command command;
    command.apply = &ApplyRemove<T>;
    command.target = entity;
    commands_.Push(command);
}

template <class T> Status CommandBuffer::ApplyAdd(Context& context, const Command& command, byte* payload)
{
    T* value = reinterpret_cast<T*>(payload);
    const Entity target = context.buffer->Resolve(command.target);
    Status status = Status::DeadEntity;
    if (target.IsValid())
    {
        status = context.world->Add<T>(target, Move(*value));
    }
    value->~T();
    return status;
}

template <class T> Status CommandBuffer::ApplySet(Context& context, const Command& command, byte* payload)
{
    T* value = reinterpret_cast<T*>(payload);
    const Entity target = context.buffer->Resolve(command.target);
    Status status = Status::DeadEntity;
    if (target.IsValid())
    {
        status = context.world->Set<T>(target, Move(*value));
    }
    value->~T();
    return status;
}

template <class T> Status CommandBuffer::ApplyRemove(Context& context, const Command& command, byte*)
{
    const Entity target = context.buffer->Resolve(command.target);
    return target.IsValid() ? context.world->Remove<T>(target) : Status::DeadEntity;
}

template <class... Ts, usize... I>
Status CommandBuffer::ApplyCreate(Context& context, const Command& command, byte* payload, detail::IndexList<I...>)
{
    Entity made;
    if constexpr (sizeof...(Ts) == 0)
    {
        made = context.world->Create();
    }
    else
    {
        constexpr detail::PackLayout<Ts...> layout = detail::ComputePackLayout<Ts...>();
        made = context.world->Create(Move(*reinterpret_cast<Ts*>(payload + layout.offset[I]))...);
        (reinterpret_cast<Ts*>(payload + layout.offset[I])->~Ts(), ...);
    }
    if (!made.IsValid())
    {
        return Status::Refused;
    }
    context.buffer->created_[command.target.index] = made;
    return Status::Ok;
}

template <class... Ts, usize... I> void CommandBuffer::MoveInto(byte* payload, detail::IndexList<I...>, Ts&... values)
{
    constexpr detail::PackLayout<Ts...> layout = detail::ComputePackLayout<Ts...>();
    (ConstructAt<Ts>(payload + layout.offset[I], Move(values)), ...);
}

template <class... Ts, usize... I> void CommandBuffer::DropPack(byte* payload, detail::IndexList<I...>)
{
    constexpr detail::PackLayout<Ts...> layout = detail::ComputePackLayout<Ts...>();
    (reinterpret_cast<Ts*>(payload + layout.offset[I])->~Ts(), ...);
}

}  // namespace vse::ecs
