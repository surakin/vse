#include "script/command_buffer.h"

#include "algorithm.h"
#include "cstr/mem_ops.h"
#include "log/assert.h"
#include "script/script_world.h"

namespace vse::script
{

namespace
{

constexpr usize kMaxCommands = usize{1} << 24;
constexpr usize kMaxPayloadBytes = usize{1} << 30;

}  // namespace

void CommandBuffer::Begin(u32 partition)
{
    commands_.Clear();
    bytes_.Clear();
    ENGINE_ASSERT(partition < kMaxPartitions, "CommandBuffer::Begin: the partition number is out of range");
    partition_ = partition;
    reserved_ = 0;
}

bool CommandBuffer::Record(u16 kind, u64 order_key, ecs::Entity target, const void* payload, u32 size)
{
    if ((payload == nullptr && size != 0) || commands_.Size() >= kMaxCommands ||
        bytes_.Size() > kMaxPayloadBytes - size)
    {
        return false;
    }
    CommandRecord record;
    record.order_key = order_key;
    record.sequence = static_cast<u32>(commands_.Size());
    record.kind = kind;
    record.target = target;
    record.payload_offset = static_cast<u32>(bytes_.Size());
    record.payload_size = size;
    if (size != 0)
    {
        const usize at = bytes_.Size();
        bytes_.Resize(at + size);
        MemCopy(bytes_.Data() + at, payload, size);
    }
    commands_.Push(record);
    return true;
}

ecs::Entity CommandBuffer::Reserve()
{
    if (reserved_ >= kPlaceholdersPerPartition)
    {
        return ecs::Entity();
    }
    ecs::Entity placeholder;
    placeholder.index = partition_ * kPlaceholdersPerPartition + reserved_;
    placeholder.generation = ecs::Entity::kPlaceholderGeneration;
    ++reserved_;
    return placeholder;
}

bool CommandBuffer::IsReserved(ecs::Entity entity) const
{
    if (!entity.IsPlaceholder())
    {
        return false;
    }
    const u32 first = partition_ * kPlaceholdersPerPartition;
    return entity.index >= first && entity.index - first < reserved_;
}

namespace
{

bool Before(const CommandRecord& a, const CommandRecord& b)
{
    return a.order_key != b.order_key ? a.order_key < b.order_key : a.sequence < b.sequence;
}

}  // namespace

bool CommandBuffer::IsSorted() const
{
    for (usize i = 1; i < commands_.Size(); ++i)
    {
        if (Before(commands_[i], commands_[i - 1]))
        {
            return false;
        }
    }
    return true;
}

void CommandBuffer::SortIfNeeded()
{
    if (!IsSorted())
    {
        // (order_key, sequence) is a total order, so an unstable sort gives the one possible result.
        Sort(Span<CommandRecord>(commands_.Data(), commands_.Size()), &Before);
    }
}

void Overlay::Put(ecs::Entity entity, u32 field, Value value)
{
    entries_.Insert(KeyOf(entity, field), Move(value));
}

const Value* Overlay::Find(ecs::Entity entity, u32 field) const
{
    return entries_.Find(KeyOf(entity, field));
}

}  // namespace vse::script
