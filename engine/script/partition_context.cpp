#include "script/partition_context.h"

namespace engine_placeholder::script
{

namespace
{

thread_local PartitionContext* g_current = nullptr;

bool ContextAlive(void*, ecs::Entity entity)
{
    const PartitionContext* context = g_current;
    if (context == nullptr)
    {
        return false;
    }
    if (entity.IsPlaceholder())
    {
        return context->commands != nullptr && context->commands->IsReserved(entity);
    }
    return context->world != nullptr && context->world->Exists(entity);
}

}  // namespace

ScopedPartitionContext::ScopedPartitionContext(PartitionContext* context) : previous_(g_current)
{
    g_current = context;
}

ScopedPartitionContext::~ScopedPartitionContext()
{
    g_current = previous_;
}

PartitionContext* CurrentPartitionContext()
{
    return g_current;
}

EntityCheck ContextEntityCheck()
{
    EntityCheck check;
    check.is_alive = &ContextAlive;
    return check;
}

}  // namespace engine_placeholder::script
