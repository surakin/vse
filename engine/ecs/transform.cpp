#include "ecs/transform.h"

#include "containers/hash_map.h"
#include "ecs/query.h"
#include "math/transform.h"
#include "mem/heap_allocator.h"
#include "sync/atomic.h"

namespace engine_placeholder::ecs {

namespace {

bool SameMatrix(const Mat4& a, const Mat4& b) {
    for (int c = 0; c < 4; ++c) {
        if (a.col[c].x != b.col[c].x || a.col[c].y != b.col[c].y || a.col[c].z != b.col[c].z || a.col[c].w != b.col[c].w) {
            return false;
        }
    }
    return true;
}

// The parent and depth of every entity that has a Parent, in iteration order.
struct Link {
    Entity entity;
    Entity parent;
};

void CollectLinks(World& world, Vector<Link>* out) {
    world.Query<Read<Parent>>().ForEach([&](Entity e, const Parent& p) { out->Push(Link{e, p.entity}); });
}

// The descendants of `root` with their distance below it, found by repeated passes over the links (each pass finds one more level).
void Descendants(const Vector<Link>& links, Entity root, HashMap<Entity, u32, HeapAllocator>* distance, u32* height) {
    distance->Insert(root, 0);
    *height = 0;
    for (bool grew = true; grew;) {
        grew = false;
        for (usize i = 0; i < links.Size(); ++i) {
            const u32* up = distance->Find(links[i].parent);
            if (up != nullptr && distance->Find(links[i].entity) == nullptr) {
                const u32 next = *up + 1; // copied first: Insert may rehash and move the slot `up` points at
                distance->Insert(links[i].entity, next);
                *height = next > *height ? next : *height;
                grew = true;
            }
        }
    }
}

// Sets the depth of every descendant of `root` (not root itself) to `root_depth` plus its distance.
void RebaseDepths(World& world, const Vector<Link>& links, Entity root, u32 root_depth) {
    HashMap<Entity, u32, HeapAllocator> distance(HeapAllocator::Default());
    u32 height = 0;
    Descendants(links, root, &distance, &height);
    for (usize i = 0; i < links.Size(); ++i) {
        const u32* d = distance.Find(links[i].entity);
        if (d != nullptr && links[i].entity != root) {
            world.Set<HierarchyDepth>(links[i].entity, HierarchyDepth{root_depth + *d});
        }
    }
}

u32 SubtreeHeight(const Vector<Link>& links, Entity root) {
    HashMap<Entity, u32, HeapAllocator> distance(HeapAllocator::Default());
    u32 height = 0;
    Descendants(links, root, &distance, &height);
    return height;
}

} // namespace

Mat4 LocalTransform::ToMatrix() const {
    Transform t;
    t.position = position;
    t.rotation = rotation;
    t.scale = scale;
    return t.ToMatrix();
}

LocalTransform Decompose(const Mat4& m) {
    LocalTransform out;
    const Vec3 x(m.col[0].x, m.col[0].y, m.col[0].z);
    const Vec3 y(m.col[1].x, m.col[1].y, m.col[1].z);
    const Vec3 z(m.col[2].x, m.col[2].y, m.col[2].z);
    out.position = Vec3(m.col[3].x, m.col[3].y, m.col[3].z);
    out.scale = Vec3(x.Length(), y.Length(), z.Length());
    if (out.scale.x > 0.0f && out.scale.y > 0.0f && out.scale.z > 0.0f) {
        Mat3 rotation;
        rotation.col[0] = x * (1.0f / out.scale.x);
        rotation.col[1] = y * (1.0f / out.scale.y);
        rotation.col[2] = z * (1.0f / out.scale.z);
        out.rotation = Quaternion::FromMat3(rotation).Normalized();
    }
    return out;
}

LocalTransform LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    LocalTransform out;
    out.position = eye;
    const Vec3 toward = target - eye;
    if (toward.LengthSquared() == 0.0f) {
        return out;
    }
    const Vec3 forward = toward.Normalized();
    const Vec3 side = forward.Cross(up);
    if (side.LengthSquared() == 0.0f) {
        return out;
    }
    const Vec3 right = side.Normalized();
    const Vec3 true_up = right.Cross(forward);
    Mat3 basis;
    basis.col[0] = right;
    basis.col[1] = true_up;
    basis.col[2] = forward * -1.0f; // the local +Z axis points away from the target, so -Z looks at it
    out.rotation = Quaternion::FromMat3(basis).Normalized();
    return out;
}

Entity CreateTransformEntity(World& world, const LocalTransform& local) {
    return world.Create(local, WorldTransform{});
}

HierarchyStatus SetParent(World& world, Entity child, Entity parent) {
    if (!world.Exists(child) || !world.Exists(parent)) {
        return HierarchyStatus::DeadEntity;
    }
    if (child == parent) {
        return HierarchyStatus::SelfParent;
    }
    // Walk up from the new parent: meeting the child means a cycle; the number of steps is the parent's depth.
    u32 parent_depth = 0;
    for (Entity up = parent;;) {
        const Parent* above = world.Peek<Parent>(up);
        if (above == nullptr) {
            break;
        }
        up = above->entity;
        ++parent_depth;
        if (up == child) {
            return HierarchyStatus::Cycle;
        }
        if (parent_depth > kMaxHierarchyDepth) {
            break; // a deeper, malformed chain: refused below
        }
    }
    Vector<Link> links;
    CollectLinks(world, &links);
    if (parent_depth + 1 + SubtreeHeight(links, child) > kMaxHierarchyDepth) {
        if (TransformReport* report = world.TryResource<TransformReport>()) {
            ++report->refused_depth;
        }
        return HierarchyStatus::TooDeep;
    }
    const bool had_parent = world.Has<Parent>(child);
    if (world.Set<Parent>(child, Parent{parent}) != Status::Ok) {
        return HierarchyStatus::Refused;
    }
    if (world.Set<HierarchyDepth>(child, HierarchyDepth{parent_depth + 1}) != Status::Ok) {
        if (!had_parent) {
            world.Remove<Parent>(child); // no half-made link: a Parent without a depth is visited by no query
        }
        return HierarchyStatus::Refused;
    }
    // The child's own migration moved no one else, but the links list predates the Set: its entries stay valid (entities, not rows).
    RebaseDepths(world, links, child, parent_depth + 1);
    return HierarchyStatus::Ok;
}

HierarchyStatus ClearParent(World& world, Entity child) {
    if (!world.Exists(child)) {
        return HierarchyStatus::DeadEntity;
    }
    if (!world.Has<Parent>(child)) {
        return HierarchyStatus::Ok;
    }
    world.Remove<Parent>(child);
    world.Remove<HierarchyDepth>(child);
    Vector<Link> links;
    CollectLinks(world, &links);
    RebaseDepths(world, links, child, 0);
    return HierarchyStatus::Ok;
}

u32 DestroyRecursive(World& world, CommandBuffer& commands, Entity root) {
    Vector<Link> links;
    CollectLinks(world, &links);
    HashMap<Entity, u32, HeapAllocator> distance(HeapAllocator::Default());
    u32 height = 0;
    Descendants(links, root, &distance, &height);
    u32 recorded = 0;
    // Deepest first, so a reader of the commands sees children go before their parent.
    for (u32 d = height + 1; d > 0; --d) {
        for (usize i = 0; i < links.Size(); ++i) {
            const u32* at = distance.Find(links[i].entity);
            if (at != nullptr && *at == d - 1 && links[i].entity != root) {
                commands.Destroy(links[i].entity);
                ++recorded;
            }
        }
    }
    commands.Destroy(root);
    return recorded + 1;
}

namespace {

using RootQuery = Query<Changed<LocalTransform>, Read<LocalTransform>, LazyWrite<WorldTransform>, Without<Parent>>;
using ChildQuery = Query<Read<Parent>, Read<HierarchyDepth>, With<LocalTransform>, With<WorldTransform>>;
using DirtyLocalQuery = Query<Changed<LocalTransform>, With<Parent>, With<HierarchyDepth>, With<WorldTransform>>;
using DirtyParentQuery = Query<Changed<Parent>, With<LocalTransform>, With<WorldTransform>>;
using DirtyDepthQuery = Query<Changed<HierarchyDepth>, With<Parent>, With<LocalTransform>, With<WorldTransform>>;

// The state of one propagation system: its queries (kept between frames so their Changed filters remember), and scratch buffers.
class Propagation {
public:
    Propagation() = default;
    Propagation(Propagation&&) = default;

    void operator()(SystemContext& context) {
        if (roots_ == nullptr || world_id_ != context.WorldId()) {
            roots_ = MakeUnique<RootQuery>(context.Query<Changed<LocalTransform>, Read<LocalTransform>, LazyWrite<WorldTransform>, Without<Parent>>());
            children_ = MakeUnique<ChildQuery>(context.Query<Read<Parent>, Read<HierarchyDepth>, With<LocalTransform>, With<WorldTransform>>());
            dirty_local_ = MakeUnique<DirtyLocalQuery>(context.Query<Changed<LocalTransform>, With<Parent>, With<HierarchyDepth>, With<WorldTransform>>());
            dirty_parent_ = MakeUnique<DirtyParentQuery>(context.Query<Changed<Parent>, With<LocalTransform>, With<WorldTransform>>());
            dirty_depth_ = MakeUnique<DirtyDepthQuery>(context.Query<Changed<HierarchyDepth>, With<Parent>, With<LocalTransform>, With<WorldTransform>>());
            world_id_ = context.WorldId();
        }
        TransformReport& report = context.Resource<TransformReport>();
        ++report.frames;

        // Roots: only chunks whose local transform changed.
        const u64 visited_before = roots_->Stats().chunks_visited;
        Atomic<u64> recomputed{0};
        Atomic<u64> written{0};
        context.ForEachChunkParallel(*roots_, [&](ChunkView& view, CommandBuffer&) {
            Span<const LocalTransform> locals = view.Read<LocalTransform>();
            Span<WorldTransform> worlds = view.Write<WorldTransform>();
            bool any = false;
            u64 wrote = 0;
            for (usize i = 0; i < locals.Size(); ++i) {
                const Mat4 m = locals[i].ToMatrix();
                if (!SameMatrix(worlds[i].matrix, m)) {
                    worlds[i].matrix = m;
                    any = true;
                    ++wrote;
                }
            }
            if (any) {
                view.MarkWritten<WorldTransform>();
            }
            recomputed.FetchAdd(locals.Size(), MemoryOrder::Relaxed);
            written.FetchAdd(wrote, MemoryOrder::Relaxed);
        });
        const bool roots_changed = written.Load(MemoryOrder::Relaxed) != 0 || roots_->Stats().chunks_visited != visited_before;

        // Children: only when something could have changed.
        // A destroyed entity leaves no mark, so any destruction since the last run means a parent may have gone: look at the children once.
        const u64 destroyed = context.DestroyedCount();
        bool dirty = roots_changed || destroyed != destroyed_seen_;
        destroyed_seen_ = destroyed;
        dirty_local_->ForEachChunk([&](ChunkView&) {
            dirty = true;
            return false;
        });
        dirty_parent_->ForEachChunk([&](ChunkView&) {
            dirty = true;
            return false;
        });
        dirty_depth_->ForEachChunk([&](ChunkView&) {
            dirty = true;
            return false;
        });
        if (dirty) {
            Children(context, &report, &recomputed, &written);
        }
        report.recomputed += recomputed.Load(MemoryOrder::Relaxed);
        report.written += written.Load(MemoryOrder::Relaxed);
        roots_->Finish();
        dirty_local_->Finish();
        dirty_parent_->Finish();
        dirty_depth_->Finish();
    }

    bool TakeStats(QueryStats* out) {
        if (roots_ == nullptr) {
            return false;
        }
        *out = roots_->Stats();
        roots_->ResetStats();
        return true;
    }

private:
    // Sorts the children into depth buckets (repairing stale depths, removing dangling parents), then computes each bucket in parallel, shallowest first.
    void Children(SystemContext& context, TransformReport* report, Atomic<u64>* recomputed, Atomic<u64>* written) {
        for (usize d = 0; d < buckets_.Size(); ++d) {
            buckets_[d].Clear();
        }
        children_->ForEachChunk([&](ChunkView& view) {
            Span<const Entity> entities = view.Entities();
            Span<const Parent> parents = view.Read<Parent>();
            Span<const HierarchyDepth> depths = view.Read<HierarchyDepth>();
            for (usize i = 0; i < entities.Size(); ++i) {
                const Entity parent = parents[i].entity;
                if (!context.Exists(parent)) {
                    context.Commands().Remove<Parent>(entities[i]);
                    context.Commands().Remove<HierarchyDepth>(entities[i]);
                    ++report->dangling_fixed;
                    continue;
                }
                u32 expected = 0;
                bool known = true;
                if (context.Peek<Parent>(parent) != nullptr) {
                    const HierarchyDepth* above = context.Peek<HierarchyDepth>(parent);
                    known = above != nullptr;
                    expected = known ? above->depth : 0;
                }
                u32 depth = depths[i].depth;
                if (!known || depth != expected + 1 || depth > kMaxHierarchyDepth) {
                    u32 resolved = 0;
                    if (!Resolve(context, entities[i], &resolved)) {
                        ++report->unresolved;
                        continue;
                    }
                    ++report->depth_repaired;
                    context.Commands().Set<HierarchyDepth>(entities[i], HierarchyDepth{resolved});
                    depth = resolved;
                }
                while (buckets_.Size() <= depth) {
                    buckets_.Push(Vector<Entity>());
                }
                buckets_[depth].Push(entities[i]);
            }
        });
        for (usize d = 1; d < buckets_.Size(); ++d) {
            Vector<Entity>& bucket = buckets_[d];
            if (bucket.Size() == 0) {
                continue;
            }
            flags_.Clear();
            flags_.Resize(bucket.Size());
            auto compute = [&](usize begin, usize end) {
                u64 wrote = 0;
                for (usize i = begin; i < end; ++i) {
                    const Entity e = bucket[i];
                    const Parent* link = context.Peek<Parent>(e);
                    const LocalTransform* local = context.Peek<LocalTransform>(e);
                    WorldTransform* mine = context.Poke<WorldTransform>(e);
                    if (link == nullptr || local == nullptr || mine == nullptr) {
                        continue;
                    }
                    const WorldTransform* above = context.Peek<WorldTransform>(link->entity);
                    const Mat4 m = above == nullptr ? local->ToMatrix() : above->matrix * local->ToMatrix();
                    if (!SameMatrix(mine->matrix, m)) {
                        mine->matrix = m;
                        flags_[i] = 1;
                        ++wrote;
                    }
                }
                written->FetchAdd(wrote, MemoryOrder::Relaxed);
            };
            context.ParallelFor(bucket.Size(), 0, compute);
            recomputed->FetchAdd(bucket.Size(), MemoryOrder::Relaxed);
            // Stamping touches shared chunk headers, so it happens here, on one thread, after the depth's jobs are done.
            for (usize i = 0; i < bucket.Size(); ++i) {
                if (flags_[i] != 0) {
                    context.Stamp<WorldTransform>(bucket[i]);
                }
            }
        }
    }

    // Follows the parent chain from `entity` to a root: its depth, or false for a cycle, a dead link, or a chain deeper than the maximum.
    static bool Resolve(SystemContext& context, Entity entity, u32* depth) {
        u32 steps = 0;
        for (Entity at = entity;;) {
            const Parent* link = context.Peek<Parent>(at);
            if (link == nullptr) {
                *depth = steps;
                return true;
            }
            if (!context.Exists(link->entity) || ++steps > kMaxHierarchyDepth) {
                return false;
            }
            at = link->entity;
        }
    }

    UniquePtr<RootQuery> roots_;
    UniquePtr<ChildQuery> children_;
    u64 destroyed_seen_ = 0;
    UniquePtr<DirtyLocalQuery> dirty_local_;
    UniquePtr<DirtyParentQuery> dirty_parent_;
    UniquePtr<DirtyDepthQuery> dirty_depth_;
    u64 world_id_ = 0;
    Vector<Vector<Entity>> buckets_;
    Vector<u8> flags_;
};

} // namespace

SystemDesc MakeTransformSystem(const char* name) {
    SystemDesc desc(name, detail::SystemFn(Propagation()));
    return Move(desc).Reads<LocalTransform, Parent, HierarchyDepth>().Writes<WorldTransform>().WritesResource<TransformReport>();
}

} // namespace engine_placeholder::ecs
