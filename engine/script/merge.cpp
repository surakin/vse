#include "script/merge.h"

#include "log/assert.h"

namespace vse::script
{

struct MergeAccess
{
    static void SetPartition(MergeContext& context, u32 partition) { context.partition_ = partition; }
};

namespace
{

struct Writer
{
    u32 partition = 0;
    u32 generation = 0;
};

}  // namespace

MergeReport Merge(ecs::World* world, Span<CommandBuffer* const> buffers, const CommandKindTable& kinds,
                  ConflictPolicy policy, void* user)
{
    MergeReport report;
    MergeContext context(world, user);
    HashMap<u64, Writer, HeapAllocator> writers(HeapAllocator::Default());
    for (usize b = 0; b < buffers.Size(); ++b)
    {
        CommandBuffer& buffer = *buffers[b];
        ENGINE_ASSERT(b == 0 || buffers[b - 1]->Partition() < buffer.Partition(),
                      "Merge: the buffers are not in increasing partition order");
        buffer.SortIfNeeded();
        MergeAccess::SetPartition(context, buffer.Partition());
        const Span<const CommandRecord> commands = buffer.Commands();
        for (usize i = 0; i < commands.Size(); ++i)
        {
            const CommandRecord& command = commands[i];
            const CommandKind* kind = kinds.Find(command.kind);
            if (kind == nullptr)
            {
                ++report.unknown;
                continue;
            }
            ecs::Entity target = command.target;
            if (!kind->creates && !kind->global)
            {
                target = context.Resolve(command.target);
                if (!target.IsValid())
                {
                    ++report.dangling;
                    continue;
                }
            }
            if (kind->writes && !kind->global)
            {
                const u64 key = (static_cast<u64>(target.index) << 16) | command.kind;
                Writer* previous = writers.Find(key);
                if (previous != nullptr && previous->generation == target.generation &&
                    previous->partition != buffer.Partition())
                {
                    ++report.conflicts;
                    if (policy == ConflictPolicy::Error)
                    {
                        ++report.refused;
                        continue;
                    }
                }
                Writer writer;
                writer.partition = buffer.Partition();
                writer.generation = target.generation;
                writers.Insert(key, writer);
            }
            if (kind->apply(context, target, buffer.PayloadOf(command)))
            {
                ++report.applied;
            }
            else
            {
                ++report.skipped;
            }
        }
    }
    return report;
}

}  // namespace vse::script
