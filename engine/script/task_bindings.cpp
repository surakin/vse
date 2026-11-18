#include "script/task_bindings.h"

#include "script/partition_context.h"
#include "script/task_pool.h"

namespace engine_placeholder::script
{

namespace
{

constexpr u64 kMaxTaskSourceBytes = 1 << 16;

Value Refuse(const char* text)
{
    return Value::FromText(StringView(text));
}

Value TaskEnqueue(i64 mode, StringView source, Value data, i64 seed, i64 count, i64 chunk)
{
    PartitionContext* context = CurrentPartitionContext();
    if (context == nullptr || context->tasks == nullptr || context->tasks->outbox == nullptr)
    {
        RaiseScriptError("tasks are not available to this script");
        return Value();
    }
    TaskPartition* view = context->tasks;
    if (mode < 1 || mode > 3)
    {
        RaiseScriptError("task: bad mode");
        return Value();
    }
    if (source.Length() == 0 || source.Length() > kMaxTaskSourceBytes)
    {
        return Refuse("a task's source is 1 to 65536 bytes");
    }
    if (chunk < 0 || chunk > 0x7FFFFFFF)
    {
        return Refuse("the chunk size must be a positive number of elements");
    }
    if (mode == static_cast<i64>(TaskMode::Map) && data.Kind() != ValueKind::Array)
    {
        return Refuse("task.map: the second argument must be an array");
    }
    if (mode == static_cast<i64>(TaskMode::For) && count < 0)
    {
        return Refuse("task.parallel_for: the count must not be negative");
    }
    TaskOutbox* outbox = view->outbox;
    if (view->held + outbox->requests.Size() >= view->queue_limit)
    {
        return Refuse("the task queue is full (setting script.task_queue_limit): wait for or take results first");
    }
    const u64 bytes = TaskValueBytes(data) + source.Length() + 64;
    if (view->arg_bytes_limit != 0 && outbox->bytes + bytes > view->arg_bytes_limit)
    {
        return Refuse("the task's arguments are too large (setting script.task_arg_bytes)");
    }
    TaskRequest request;
    request.id = (static_cast<u64>(view->partition) + 1) << kTaskIdShift | outbox->next_sequence;
    request.owner_key = context->current_entity_key;
    request.mode = static_cast<TaskMode>(mode);
    request.source = String<HeapAllocator>(HeapAllocator::Default(), source);
    request.data = Move(data);
    request.seed = static_cast<u64>(seed);
    request.count = static_cast<u64>(count);
    request.chunk = static_cast<u32>(chunk);
    request.bytes = bytes;
    ++outbox->next_sequence;
    outbox->bytes += bytes;
    const u64 id = request.id;
    outbox->requests.Push(Move(request));
    return Value::FromInt(static_cast<i64>(id));
}

Value TaskTake(i64 handle)
{
    PartitionContext* context = CurrentPartitionContext();
    if (context == nullptr || context->tasks == nullptr)
    {
        RaiseScriptError("tasks are not available to this script");
        return Value();
    }
    Value out;
    const TakeStatus status = TakeTask(context->tasks, context->current_entity_key, static_cast<u64>(handle), &out);
    Value pair = Value::MakeArray();
    pair.Push(Value::FromInt(static_cast<i64>(status)));
    pair.Push(Move(out));
    return pair;
}

}  // namespace

bool RegisterTaskBindings(BindingRegistry* registry)
{
    bool ok = true;
    ok &= registry->Add<&TaskEnqueue>("task", "enqueue", ThreadRule::WorldWrite,
                                      "mode, source, data, seed, count, chunk",
                                      "Queues a task (the host behind `task.run`, `task.map` and `task.parallel_for`); "
                                      "returns its handle, or text saying why it was refused. It runs at the end of "
                                      "the tick.");
    ok &= registry->Add<&TaskTake>("task", "take", ThreadRule::WorldWrite, "handle",
                                   "Takes a finished task's result (the host behind `task.result`): returns "
                                   "`{ status, value }`.");
    return ok;
}

}  // namespace engine_placeholder::script
