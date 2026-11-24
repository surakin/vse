#include "scene/instance_wind.h"

#include "containers/unique_ptr.h"
#include "ecs/query.h"
#include "lang/scalar.h"
#include "scene/components.h"
#include "scene/render_sync.h"

namespace vse::scene
{

namespace
{

constexpr f32 kTwoPi = 6.28318530717958647692f;

u32 WindHash(u32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

using WindQuery = ecs::Query<ecs::Read<InstanceHierarchy>>;

class WindSystem
{
public:
    WindSystem() = default;
    WindSystem(WindSystem&&) = default;

    void operator()(ecs::SystemContext& context)
    {
        if (query_ == nullptr || world_id_ != context.WorldId())
        {
            query_ = MakeUnique<WindQuery>(context.Query<ecs::Read<InstanceHierarchy>>());
            world_id_ = context.WorldId();
        }
        const WindState wind = context.ReadResource<WindState>();
        query_->ForEach(
            [&](ecs::Entity, const InstanceHierarchy& h)
            {
                renderer::InstanceTree* tree = h.tree.Get();
                if (tree == nullptr)
                {
                    return;
                }
                for (u32 i = 0; i < tree->NodeCount(); ++i)
                {
                    const renderer::InstanceTreeNode& node = tree->Node(i);
                    if ((node.flags & kTreeNodeWind) == 0)
                    {
                        continue;
                    }
                    const f32 half = WindAngle(wind, node.id) * 0.5f;
                    const Quaternion q(Sin(half), 0.0f, 0.0f, Cos(half));
                    const Vec3d position = node.parent == renderer::kTreeNone ? node.origin
                                                                              : Vec3d(node.local.position);
                    // A node whose angle did not change stays clean, so a calm wind recomposes nothing.
                    if (node.local.rotation.x == q.x && node.local.rotation.y == q.y && node.local.rotation.z == q.z &&
                        node.local.rotation.w == q.w)
                    {
                        continue;
                    }
                    renderer::TreeNodeId id;
                    id.value = node.id;
                    tree->SetLocal(id, position, q, node.local.scale);
                }
            });
        query_->Finish();
    }

private:
    UniquePtr<WindQuery> query_;
    u64 world_id_ = 0;
};

}  // namespace

f32 WindAngle(const WindState& wind, u32 id)
{
    const f32 phase = static_cast<f32>(WindHash(id) >> 8) / 16777216.0f;
    return wind.amplitude * Sin(kTwoPi * (wind.frequency * wind.time + phase));
}

ecs::SystemDesc MakeWindSystem(const char* name)
{
    ecs::SystemDesc desc(name, ecs::detail::SystemFn(WindSystem()));
    // The system edits the shared trees, which the hierarchy sync recomposes and reads: declaring the link as written
    // orders the two (never in parallel), in registration order.
    return Move(desc).Reads<InstanceHierarchy>().ReadsResource<WindState>().WritesResource<scene::SceneLink>();
}

}  // namespace vse::scene
