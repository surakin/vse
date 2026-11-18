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

namespace
{

constexpr u64 kGolden = 0x9E3779B97F4A7C15ull;

u64 MixBits(u64 z)
{
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

u64 SplitMix(u64 z)
{
    return MixBits(z + kGolden);
}

}  // namespace

u64 AdvanceRandom(u64* state)
{
    *state += kGolden;
    return MixBits(*state);
}

u64 RandomSeedFor(u32 world_seed, u64 entity_key, u64 tick)
{
    return SplitMix(SplitMix(SplitMix(u64{world_seed}) ^ entity_key) ^ tick);
}

void SeedPartitionRandom(PartitionContext* context)
{
    context->random_state = RandomSeedFor(context->world_seed, context->current_entity_key, context->tick);
}

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
