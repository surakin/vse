#include "ecs/command_buffer.h"

namespace engine_placeholder::ecs
{

CommandBuffer::~CommandBuffer()
{
    Clear();
    for (usize i = 0; i < blocks_.Size(); ++i)
    {
        HeapAllocator::Default().Free(blocks_[i], kBlockBytes, kMaxAlign);
    }
}

byte* CommandBuffer::Reserve(usize size, usize align, u32* block, u32* offset)
{
    if (size > kBlockBytes)
    {
        return nullptr;
    }
    usize at = (used_ + align - 1) / align * align;
    if (blocks_.Size() == 0 || at + size > kBlockBytes)
    {
        // The next block: the following one if an earlier use made it, else a new one.
        const u32 next = blocks_.Size() == 0 ? 0 : current_block_ + 1;
        if (next >= blocks_.Size())
        {
            void* memory = HeapAllocator::Default().Alloc(kBlockBytes, kMaxAlign);
            if (memory == nullptr)
            {
                return nullptr;
            }
            blocks_.Push(static_cast<byte*>(memory));
        }
        current_block_ = next;
        used_ = 0;
        at = 0;
    }
    *block = current_block_;
    *offset = static_cast<u32>(at);
    used_ = at + size;
    return blocks_[current_block_] + at;
}

Entity CommandBuffer::NewPlaceholder()
{
    const Entity placeholder{static_cast<u32>(created_.Size()), Entity::kPlaceholderGeneration};
    created_.Push(Entity{});
    return placeholder;
}

Entity CommandBuffer::Create()
{
    Command command;
    command.apply = [](Context& c, const Command& cmd, byte* p)
    { return ApplyCreate<>(c, cmd, p, detail::IndexList<>{}); };
    command.creates = true;
    command.target = NewPlaceholder();
    commands_.Push(command);
    return command.target;
}

void CommandBuffer::Destroy(Entity entity)
{
    Command command;
    command.apply = &ApplyDestroy;
    command.target = entity;
    commands_.Push(command);
}

Status CommandBuffer::ApplyDestroy(Context& context, const Command& command, byte*)
{
    const Entity target = context.buffer->Resolve(command.target);
    return target.IsValid() ? context.world->Destroy(target) : Status::DeadEntity;
}

Entity CommandBuffer::Resolve(Entity entity) const
{
    if (!entity.IsPlaceholder())
    {
        return entity;
    }
    return entity.index < created_.Size() ? created_[entity.index] : Entity{};
}

PlaybackReport CommandBuffer::Play(World& world)
{
    PlaybackReport report;
    Context context{&world, this};
    for (usize i = 0; i < commands_.Size(); ++i)
    {
        const Command& command = commands_[i];
        const Status status = command.apply(context, command, PayloadOf(command));
        if (status == Status::Ok)
        {
            ++report.applied;
            if (command.creates)
            {
                ++report.created;
            }
        }
        else
        {
            ++report.ignored;
        }
    }
    commands_.Clear();
    current_block_ = 0;
    used_ = 0;
    return report;
}

void CommandBuffer::Clear()
{
    for (usize i = 0; i < commands_.Size(); ++i)
    {
        const Command& command = commands_[i];
        if (command.drop != nullptr)
        {
            command.drop(PayloadOf(command));
        }
    }
    commands_.Clear();
    created_.Clear();
    current_block_ = 0;
    used_ = 0;
}

PlaybackReport World::Apply(CommandBuffer& buffer)
{
    return buffer.Play(*this);
}

CommandBufferPool::CommandBufferPool(u32 count)
{
    ENGINE_ASSERT(count >= 1, "CommandBufferPool needs at least one buffer");
    for (u32 i = 0; i < count; ++i)
    {
        buffers_.Push(MakeUnique<CommandBuffer>());
    }
}

void CommandBufferPool::EnsureCount(u32 count)
{
    while (buffers_.Size() < count)
    {
        buffers_.Push(MakeUnique<CommandBuffer>());
    }
}

CommandBuffer& CommandBufferPool::At(u32 index)
{
    ENGINE_ASSERT(index < buffers_.Size(), "CommandBufferPool::At: no such buffer");
    return *buffers_[index];
}

PlaybackReport CommandBufferPool::ApplyAll(World& world)
{
    PlaybackReport total;
    for (usize i = 0; i < buffers_.Size(); ++i)
    {
        total += world.Apply(*buffers_[i]);
    }
    return total;
}

void CommandBufferPool::ClearAll()
{
    for (usize i = 0; i < buffers_.Size(); ++i)
    {
        buffers_[i]->Clear();
    }
}

}  // namespace engine_placeholder::ecs
