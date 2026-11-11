#include "bench.h"

#include "algorithm.h"
#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "ecs/query.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "jobs/scheduler.h"
#include "lang/scalar.h"
#include "math/matrix.h"
#include "math/vector.h"
#include "os/os.h"

namespace ecs_bench
{

namespace
{

// ---- the shared work: both implementations call these, so they do the same arithmetic
// -------------------------------------------------------

constexpr f32 kDt = 1.0f / 60.0f;

struct Spring
{
    f32 x = 0.0f;
    f32 v = 0.0f;
    f32 target = 0.0f;
};

inline void StepPosition(Vec3& p, const Vec3& v, f32 dt)
{
    p = p + v * dt;
}
inline void StepSpring(Spring& s, f32 dt)
{
    const f32 a = -40.0f * (s.x - s.target) - 4.0f * s.v;
    s.v += a * dt;
    s.x += s.v * dt;
}
inline Mat4 MakeMatrix(const Vec3& p, f32 spring_x)
{
    const f32 k = 1.0f + 0.1f * spring_x;
    return Mat4::FromBasis(Vec3(k, 0, 0), Vec3(0, k, 0), Vec3(0, 0, k), p);
}
inline void StepTagged(Vec3& p, f32 dt)
{
    p.y += 0.5f * dt;
}
// The heavy per-entity computation of the scaling scenario (about a hundred nanoseconds).
inline f32 Heavy(f32 v)
{
    for (int i = 0; i < 24; ++i)
    {
        v = v * 0.999f + Sqrt(v * v + 1.0f) * 0.001f;
    }
    return v;
}

struct Lcg
{
    u32 state;
    explicit Lcg(u32 seed) : state(seed * 2654435761u + 12345u) {}
    u32 Next()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    f32 Unit() { return static_cast<f32>(Next()) / static_cast<f32>(1u << 24); }
    f32 Range(f32 lo, f32 hi) { return lo + (hi - lo) * Unit(); }
    u32 Below(u32 n) { return Next() % n; }
};

// The initial state of entity `i`.
struct Initial
{
    Vec3 position;
    Vec3 velocity;
    Spring spring;
    bool active;
};
Initial InitialOf(u32 i, u32 seed)
{
    Lcg rng(seed * 7919u + i);
    Initial s;
    const f32 px = rng.Range(-50.0f, 50.0f);
    const f32 py = rng.Range(-50.0f, 50.0f);
    const f32 pz = rng.Range(-50.0f, 50.0f);
    s.position = Vec3(px, py, pz);
    const f32 vx = rng.Range(-1.0f, 1.0f);
    const f32 vy = rng.Range(-1.0f, 1.0f);
    const f32 vz = rng.Range(-1.0f, 1.0f);
    s.velocity = Vec3(vx, vy, vz);
    s.spring.x = rng.Range(-1.0f, 1.0f);
    s.active = (i % 2) == 0;
    return s;
}

template <class Frame> Timing TimeFrames(u32 warmup, u32 frames, Frame&& frame)
{
    for (u32 i = 0; i < warmup; ++i)
    {
        frame();
    }
    Vector<double> samples;
    samples.Reserve(frames);
    for (u32 i = 0; i < frames; ++i)
    {
        const u64 t0 = os::MonotonicTimeNanos();
        frame();
        samples.Push(static_cast<double>(os::MonotonicTimeNanos() - t0) * 1e-6);
    }
    return Summarize(&samples);
}

jobs::Scheduler::Config ConfigFor(usize participants)
{
    jobs::Scheduler::Config config;
    config.participants = participants;
    config.pin = false;
    return config;
}

// ---- the ECS components
// ------------------------------------------------------------------------------------------------------------------

struct Pos
{
    Vec3 v;
};
struct Vel
{
    Vec3 v;
};
struct Spr
{
    Spring s;
};
struct Mtx
{
    Mat4 m;
};
struct Active
{
};
struct Rare
{
};

// ---- the object-oriented baseline
// -------------------------------------------------------------------------------------------------------

class Component
{
public:
    virtual ~Component() = default;
    virtual void Update(f32 dt) = 0;
};

class SpringComponent final : public Component
{
public:
    Spring s;
    void Update(f32 dt) override { StepSpring(s, dt); }
};

class GameObject
{
public:
    virtual ~GameObject() = default;
    virtual void Update(f32 dt)
    {
        StepPosition(pos, vel, dt);
        for (usize i = 0; i < extras.Size(); ++i)
        {
            extras[i]->Update(dt);
        }
        matrix = MakeMatrix(pos, static_cast<SpringComponent*>(extras[0].Get())->s.x);
        if (active)
        {
            StepTagged(pos, dt);
        }
    }
    Vec3 pos;
    Vec3 vel;
    bool active = false;
    bool rare = false;
    bool dirty = false;
    Mat4 matrix = Mat4::Identity();
    Vector<UniquePtr<Component>> extras;
};

UniquePtr<GameObject> MakeObject(u32 i, u32 seed)
{
    const Initial s = InitialOf(i, seed);
    UniquePtr<GameObject> o = MakeUnique<GameObject>();
    o->pos = s.position;
    o->vel = s.velocity;
    o->active = s.active;
    UniquePtr<SpringComponent> spring = MakeUnique<SpringComponent>();
    spring->s = s.spring;
    o->extras.Push(UniquePtr<Component>(spring.Release()));
    return o;
}

// All the objects, allocated in creation order and then visited in a shuffled order (the pointers are shuffled, the
// memory is not).
struct Population
{
    Vector<UniquePtr<GameObject>> visit;  // shuffled
    Vector<GameObject*> by_index;         // creation order
    void Build(u32 n, u32 seed)
    {
        visit.Reserve(n);
        by_index.Reserve(n);
        for (u32 i = 0; i < n; ++i)
        {
            visit.Push(MakeObject(i, seed));
            by_index.Push(visit[i].Get());
        }
        Lcg rng(seed ^ 0x9e3779b9u);
        for (usize i = visit.Size(); i > 1; --i)
        {
            const usize j = rng.Below(static_cast<u32>(i));
            UniquePtr<GameObject> t = Move(visit[i - 1]);
            visit[i - 1] = Move(visit[j]);
            visit[j] = Move(t);
        }
    }
};

double ObjectSum(const Population& p)
{
    double sum = 0.0;
    for (usize i = 0; i < p.visit.Size(); ++i)
    {
        const GameObject& o = *p.visit[i];
        sum += static_cast<double>(o.pos.x) + o.pos.y + o.pos.z;
    }
    return sum;
}

}  // namespace

// ---- scenario 1: iteration
// ---------------------------------------------------------------------------------------------------------------

Result RunIteration(Impl impl, const Options& options)
{
    Result r;
    if (impl == Impl::Oop)
    {
        Population pop;
        pop.Build(options.entities, options.seed);
        r.time = TimeFrames(options.warmup, options.frames,
                            [&]
                            {
                                for (usize i = 0; i < pop.visit.Size(); ++i)
                                {
                                    pop.visit[i]->Update(kDt);
                                }
                            });
        double sum = 0.0;
        for (usize i = 0; i < pop.visit.Size(); ++i)
        {
            const GameObject& o = *pop.visit[i];
            sum += static_cast<double>(o.pos.x) + o.pos.y + o.pos.z +
                   static_cast<SpringComponent*>(o.extras[0].Get())->s.x + o.matrix.col[3].x;
        }
        r.checksum = sum;
        r.count = options.entities;
        return r;
    }
    ecs::World world;
    for (u32 i = 0; i < options.entities; ++i)
    {
        const Initial s = InitialOf(i, options.seed);
        if (s.active)
        {
            world.Create(Pos{s.position}, Vel{s.velocity}, Spr{s.spring}, Mtx{Mat4::Identity()}, Active{});
        }
        else
        {
            world.Create(Pos{s.position}, Vel{s.velocity}, Spr{s.spring}, Mtx{Mat4::Identity()});
        }
    }
    auto move = world.Query<ecs::Write<Pos>, ecs::Read<Vel>>();
    auto spring = world.Query<ecs::Write<Spr>>();
    auto matrix = world.Query<ecs::Read<Pos>, ecs::Read<Spr>, ecs::Write<Mtx>>();
    auto tagged = world.Query<ecs::Write<Pos>, ecs::With<Active>>();
    r.time = TimeFrames(options.warmup, options.frames,
                        [&]
                        {
                            move.ForEach([](Pos& p, const Vel& v) { StepPosition(p.v, v.v, kDt); });
                            spring.ForEach([](Spr& s) { StepSpring(s.s, kDt); });
                            matrix.ForEach([](const Pos& p, const Spr& s, Mtx& m) { m.m = MakeMatrix(p.v, s.s.x); });
                            tagged.ForEach([](Pos& p) { StepTagged(p.v, kDt); });
                        });
    double sum = 0.0;
    world.Query<ecs::Read<Pos>, ecs::Read<Spr>, ecs::Read<Mtx>>().ForEach(
        [&](const Pos& p, const Spr& s, const Mtx& m)
        { sum += static_cast<double>(p.v.x) + p.v.y + p.v.z + s.s.x + m.m.col[3].x; });
    r.checksum = sum;
    r.count = world.Stats().entities;
    r.valid = world.Validate().Ok();
    return r;
}

// ---- scenario 2: the heavy system, scaled over workers
// ----------------------------------------------------------------------------------

Result RunHeavy(Impl impl, const Options& options)
{
    Result r;
    if (impl == Impl::Oop)
    {
        Population pop;
        pop.Build(options.entities, options.seed);
        r.time = TimeFrames(options.warmup, options.frames,
                            [&]
                            {
                                for (usize i = 0; i < pop.visit.Size(); ++i)
                                {
                                    GameObject& o = *pop.visit[i];
                                    o.pos.x = Heavy(o.pos.x);
                                }
                            });
        r.checksum = ObjectSum(pop);
        r.count = options.entities;
        return r;
    }
    ecs::World world;
    for (u32 i = 0; i < options.entities; ++i)
    {
        world.Create(Pos{InitialOf(i, options.seed).position});
    }
    jobs::Scheduler jobs(ConfigFor(options.participants));
    ecs::Schedule schedule;
    schedule.Add(ecs::MakeQuerySystem<ecs::Write<Pos>>(
        "heavy",
        [](ecs::SystemContext& context, ecs::Query<ecs::Write<Pos>>& query)
        {
            context.ForEachChunkParallel(query,
                                         [](ecs::ChunkView& view, ecs::CommandBuffer&)
                                         {
                                             Span<Pos> positions = view.Write<Pos>();
                                             for (usize i = 0; i < positions.Size(); ++i)
                                             {
                                                 positions[i].v.x = Heavy(positions[i].v.x);
                                             }
                                         });
        }));
    schedule.SetScheduler(&jobs);
    if (!schedule.Build().ok)
    {
        r.valid = false;
        return r;
    }
    r.time = TimeFrames(options.warmup, options.frames, [&] { schedule.Run(world, kDt); });
    double sum = 0.0;
    world.Query<ecs::Read<Pos>>().ForEach([&](const Pos& p) { sum += static_cast<double>(p.v.x) + p.v.y + p.v.z; });
    r.checksum = sum;
    r.count = world.Stats().entities;
    r.valid = world.Validate().Ok();
    return r;
}

// ---- scenario 3: structural churn
// --------------------------------------------------------------------------------------------------------

Result RunChurn(Impl impl, const Options& options)
{
    Result r;
    const u32 n = options.entities;
    const u32 batch = n / 10 == 0 ? 1 : n / 10;
    Lcg rng(options.seed);
    if (impl == Impl::Oop)
    {
        Population pop;
        pop.Build(n, options.seed);
        u32 next = n;
        r.time = TimeFrames(options.warmup, options.frames,
                            [&]
                            {
                                for (u32 k = 0; k < batch; ++k)
                                {
                                    const u32 slot = rng.Below(n);
                                    pop.visit[slot] = MakeObject(
                                        next++, options.seed);  // destroys the old object, allocates the new one
                                }
                                for (u32 k = 0; k < batch; ++k)
                                {
                                    GameObject& o = *pop.visit[rng.Below(n)];
                                    o.active = !o.active;
                                }
                            });
        r.count = pop.visit.Size();
        r.checksum = static_cast<double>(r.count);
        return r;
    }
    ecs::World world;
    Vector<ecs::Entity> ids;
    ids.Reserve(n);
    auto make = [&](u32 i)
    {
        const Initial s = InitialOf(i, options.seed);
        return world.Create(Pos{s.position}, Vel{s.velocity}, Spr{s.spring}, Mtx{Mat4::Identity()});
    };
    for (u32 i = 0; i < n; ++i)
    {
        ids.Push(make(i));
    }
    u32 next = n;
    r.time = TimeFrames(options.warmup, options.frames,
                        [&]
                        {
                            for (u32 k = 0; k < batch; ++k)
                            {
                                const u32 slot = rng.Below(n);
                                world.Destroy(ids[slot]);
                                ids[slot] = make(next++);
                            }
                            for (u32 k = 0; k < batch; ++k)
                            {
                                const ecs::Entity e = ids[rng.Below(n)];
                                if (world.Has<Active>(e))
                                {
                                    world.Remove<Active>(e);
                                }
                                else
                                {
                                    world.Add<Active>(e, Active{});
                                }
                            }
                        });
    r.count = world.Stats().entities;
    r.checksum = static_cast<double>(r.count);
    r.valid = world.Validate().Ok() && world.Stats().refused == 0;
    return r;
}

// ---- scenario 4: sparse access
// -----------------------------------------------------------------------------------------------------------

Result RunSparse(Impl impl, const Options& options)
{
    Result r;
    if (impl == Impl::Oop)
    {
        Population pop;
        pop.Build(options.entities, options.seed);
        for (u32 i = 0; i < options.entities; i += 100)
        {
            pop.by_index[i]->rare = true;
        }
        r.time = TimeFrames(options.warmup, options.frames,
                            [&]
                            {
                                for (usize i = 0; i < pop.visit.Size(); ++i)
                                {
                                    GameObject& o = *pop.visit[i];
                                    if (o.rare)
                                    {
                                        StepPosition(o.pos, o.vel, kDt);
                                    }
                                }
                            });
        r.checksum = ObjectSum(pop);
        r.count = options.entities;
        return r;
    }
    ecs::World world;
    for (u32 i = 0; i < options.entities; ++i)
    {
        const Initial s = InitialOf(i, options.seed);
        if (i % 100 == 0)
        {
            world.Create(Pos{s.position}, Vel{s.velocity}, Rare{});
        }
        else
        {
            world.Create(Pos{s.position}, Vel{s.velocity});
        }
    }
    auto rare = world.Query<ecs::Write<Pos>, ecs::Read<Vel>, ecs::With<Rare>>();
    r.time = TimeFrames(options.warmup, options.frames,
                        [&] { rare.ForEach([](Pos& p, const Vel& v) { StepPosition(p.v, v.v, kDt); }); });
    double sum = 0.0;
    world.Query<ecs::Read<Pos>>().ForEach([&](const Pos& p) { sum += static_cast<double>(p.v.x) + p.v.y + p.v.z; });
    r.checksum = sum;
    r.count = world.Stats().entities;
    r.valid = world.Validate().Ok();
    return r;
}

// ---- scenario 5: Changed<T>
// --------------------------------------------------------------------------------------------------------------

Result RunChanged(Impl impl, const Options& options, bool clustered)
{
    Result r;
    const u32 n = options.entities;
    const u32 writes = n / 100 == 0 ? 1 : n / 100;
    Lcg rng(options.seed);
    auto pick = [&](u32 k) { return clustered ? k : rng.Below(n); };
    if (impl == Impl::Oop)
    {
        Population pop;
        pop.Build(n, options.seed);
        u64 visited = 0;
        r.time = TimeFrames(options.warmup, options.frames,
                            [&]
                            {
                                for (u32 k = 0; k < writes; ++k)
                                {
                                    GameObject& o = *pop.by_index[pick(k)];
                                    o.pos.x += 1.0f;
                                    o.dirty = true;
                                }
                                for (usize i = 0; i < pop.visit.Size(); ++i)
                                {
                                    GameObject& o = *pop.visit[i];
                                    if (o.dirty)
                                    {
                                        ++visited;
                                        o.dirty = false;
                                    }
                                }
                            });
        r.checksum = ObjectSum(pop);
        r.count = visited;
        return r;
    }
    ecs::World world;
    Vector<ecs::Entity> ids;
    for (u32 i = 0; i < n; ++i)
    {
        ids.Push(world.Create(Pos{InitialOf(i, options.seed).position}));
    }
    auto reader = world.Query<ecs::Changed<Pos>, ecs::Read<Pos>>();
    reader.ForEach([](const Pos&) {});
    reader.Finish();  // the creation is not a change to read
    u64 visited = 0;
    r.time = TimeFrames(options.warmup, options.frames,
                        [&]
                        {
                            for (u32 k = 0; k < writes; ++k)
                            {
                                world.Get<Pos>(ids[pick(k)])->v.x += 1.0f;
                            }
                            reader.ForEach([&](const Pos&) { ++visited; });
                            reader.Finish();
                        });
    double sum = 0.0;
    world.Query<ecs::Read<Pos>>().ForEach([&](const Pos& p) { sum += static_cast<double>(p.v.x) + p.v.y + p.v.z; });
    r.checksum = sum;
    r.count = visited;
    r.valid = world.Validate().Ok();
    return r;
}

// ---- scenario 6: hierarchy
// ---------------------------------------------------------------------------------------------------------------

namespace
{

struct Node
{
    ecs::LocalTransform local;
    Mat4 world = Mat4::Identity();
    Vector<Node*> children;
};

void UpdateNode(Node* node, const Mat4* parent)
{
    node->world = parent == nullptr ? node->local.ToMatrix() : *parent * node->local.ToMatrix();
    for (usize i = 0; i < node->children.Size(); ++i)
    {
        UpdateNode(node->children[i], &node->world);
    }
}

constexpr u32 kChain = 5;

}  // namespace

Result RunHierarchy(Impl impl, const Options& options)
{
    Result r;
    const u32 roots = options.entities / kChain == 0 ? 1 : options.entities / kChain;
    if (impl == Impl::Oop)
    {
        Vector<UniquePtr<Node>> nodes;
        Vector<Node*> root_list;
        for (u32 t = 0; t < roots; ++t)
        {
            Node* prev = nullptr;
            for (u32 d = 0; d < kChain; ++d)
            {
                nodes.Push(MakeUnique<Node>());
                Node* node = nodes[nodes.Size() - 1].Get();
                node->local.position = d == 0 ? Vec3(static_cast<f32>(t), 0, 0) : Vec3(0, 1, 0);
                if (prev == nullptr)
                {
                    root_list.Push(node);
                }
                else
                {
                    prev->children.Push(node);
                }
                prev = node;
            }
        }
        Lcg rng(options.seed);
        for (usize i = root_list.Size(); i > 1; --i)
        {
            const usize j = rng.Below(static_cast<u32>(i));
            Node* t = root_list[i - 1];
            root_list[i - 1] = root_list[j];
            root_list[j] = t;
        }
        r.time = TimeFrames(options.warmup, options.frames,
                            [&]
                            {
                                for (usize i = 0; i < root_list.Size(); ++i)
                                {
                                    root_list[i]->local.position.x += 0.01f;
                                }
                                for (usize i = 0; i < root_list.Size(); ++i)
                                {
                                    UpdateNode(root_list[i], nullptr);
                                }
                            });
        double sum = 0.0;
        for (usize i = 0; i < nodes.Size(); ++i)
        {
            sum += static_cast<double>(nodes[i]->world.col[3].x) + nodes[i]->world.col[3].y;
        }
        r.checksum = sum;
        r.count = nodes.Size();
        return r;
    }
    ecs::World world;
    world.SetResource(ecs::TransformReport{});
    for (u32 t = 0; t < roots; ++t)
    {
        ecs::LocalTransform at;
        at.position = Vec3(static_cast<f32>(t), 0, 0);
        ecs::Entity prev = ecs::CreateTransformEntity(world, at);
        for (u32 d = 1; d < kChain; ++d)
        {
            ecs::LocalTransform up;
            up.position = Vec3(0, 1, 0);
            const ecs::Entity e = ecs::CreateTransformEntity(world, up);
            ecs::SetParent(world, e, prev);
            prev = e;
        }
    }
    ecs::Schedule schedule;
    schedule.Add(ecs::MakeTransformSystem("transforms"));
    if (!schedule.Build().ok)
    {
        r.valid = false;
        return r;
    }
    auto movers = world.Query<ecs::Write<ecs::LocalTransform>, ecs::Without<ecs::Parent>>();
    r.time = TimeFrames(options.warmup, options.frames,
                        [&]
                        {
                            movers.ForEach([](ecs::LocalTransform& t) { t.position.x += 0.01f; });
                            schedule.Run(world, kDt);
                        });
    double sum = 0.0;
    world.Query<ecs::Read<ecs::WorldTransform>>().ForEach(
        [&](const ecs::WorldTransform& w) { sum += static_cast<double>(w.matrix.col[3].x) + w.matrix.col[3].y; });
    r.checksum = sum;
    r.count = world.Stats().entities;
    r.valid = world.Validate().Ok() && world.Resource<ecs::TransformReport>().unresolved == 0;
    return r;
}

// ---- scenario 7: memory and lifecycle throughput
// -----------------------------------------------------------------------------------------

Lifecycle RunLifecycle(Impl impl, const Options& options)
{
    Lifecycle out;
    const u32 n = options.entities;
    if (impl == Impl::Oop)
    {
        Population pop;
        pop.visit.Reserve(n);
        const u64 t0 = os::MonotonicTimeNanos();
        for (u32 i = 0; i < n; ++i)
        {
            pop.visit.Push(MakeObject(i, options.seed));
        }
        const u64 t1 = os::MonotonicTimeNanos();
        pop.visit.Clear();
        const u64 t2 = os::MonotonicTimeNanos();
        out.create_ns = static_cast<double>(t1 - t0) / n;
        out.destroy_ns = static_cast<double>(t2 - t1) / n;
        // An estimate: the object, its spring component, the owning pointer slots (the visit list and the extras
        // buffer's one slot), and about 16 bytes of allocator header for each of the three heap blocks (object, spring,
        // extras buffer).
        out.bytes_per_entity = static_cast<double>(sizeof(GameObject) + sizeof(SpringComponent) + 2 * sizeof(void*) +
                                                   sizeof(void*) + 3 * 16);
        return out;
    }
    ecs::World world;
    Vector<ecs::Entity> ids;
    ids.Reserve(n);
    const u64 t0 = os::MonotonicTimeNanos();
    for (u32 i = 0; i < n; ++i)
    {
        const Initial s = InitialOf(i, options.seed);
        ids.Push(world.Create(Pos{s.position}, Vel{s.velocity}, Spr{s.spring}, Mtx{Mat4::Identity()}));
    }
    const u64 t1 = os::MonotonicTimeNanos();
    out.bytes_per_entity = static_cast<double>(world.Stats().pool.bytes) / n;
    for (u32 i = 0; i < n; ++i)
    {
        world.Destroy(ids[i]);
    }
    const u64 t2 = os::MonotonicTimeNanos();
    out.create_ns = static_cast<double>(t1 - t0) / n;
    out.destroy_ns = static_cast<double>(t2 - t1) / n;
    return out;
}

// ---- scenario 8: change stamping
// --------------------------------------------------------------------------------------------------------

Stamping RunStamping(const Options& options)
{
    Stamping out;
    ecs::World world;
    for (u32 i = 0; i < options.entities; ++i)
    {
        world.Create(Pos{InitialOf(i, options.seed).position});
    }
    auto reader = world.Query<ecs::Read<Pos>>();
    auto writer = world.Query<ecs::Write<Pos>>();
    float sink = 0.0f;
    out.read = TimeFrames(options.warmup, options.frames,
                          [&] { reader.ForEach([&](const Pos& p) { sink += p.v.x; }); });
    out.write = TimeFrames(options.warmup, options.frames, [&] { writer.ForEach([&](Pos& p) { sink += p.v.x; }); });
    if (sink == 12345.678f)
    {  // keeps the passes from being optimized away
        out.read.median_ms += 1e-12;
    }
    return out;
}

// ---- helpers
// ---------------------------------------------------------------------------------------------------------------------

bool ChecksumsMatch(double a, double b)
{
    const double scale = Abs(a) > Abs(b) ? Abs(a) : Abs(b);
    return Abs(a - b) <= 1e-4 * (scale > 1.0 ? scale : 1.0);
}

Timing Summarize(Vector<double>* samples)
{
    Timing t;
    if (samples->Size() == 0)
    {
        return t;
    }
    Sort(Span<double>(samples->Data(), samples->Size()));
    t.median_ms = (*samples)[samples->Size() / 2];
    t.p95_ms = (*samples)[(samples->Size() * 95) / 100 < samples->Size() ? (samples->Size() * 95) / 100
                                                                         : samples->Size() - 1];
    return t;
}

Row MakeRow(const char* scenario, Impl impl, const Options& options, const Result& result)
{
    Row row;
    FormatTo(row.scenario, sizeof(row.scenario), "%s", scenario);
    FormatTo(row.impl, sizeof(row.impl), "%s", impl == Impl::Ecs ? "ecs" : "oop");
    row.entities = options.entities;
    row.participants = options.participants;
    row.median_ms = result.time.median_ms;
    row.p95_ms = result.time.p95_ms;
    row.checksum = result.checksum;
    return row;
}

Vector<char> FormatCsv(const Vector<Row>& rows)
{
    Vector<char> out;
    const char* header = "scenario,impl,entities,participants,median_ms,p95_ms,checksum\n";
    for (const char* c = header; *c != '\0'; ++c)
    {
        out.Push(*c);
    }
    for (usize i = 0; i < rows.Size(); ++i)
    {
        char line[256];
        const Row& r = rows[i];
        const int n = FormatTo(line, sizeof(line), "%s,%s,%llu,%llu,%.9g,%.9g,%.17g\n", r.scenario, r.impl,
                               static_cast<unsigned long long>(r.entities),
                               static_cast<unsigned long long>(r.participants), r.median_ms, r.p95_ms, r.checksum);
        if (n < 0 || static_cast<usize>(n) >= sizeof(line))
        {
            return Vector<char>();
        }
        for (int k = 0; k < n; ++k)
        {
            out.Push(line[k]);
        }
    }
    return out;
}

bool ParseCsv(const char* text, usize size, Vector<Row>* rows)
{
    rows->Clear();
    usize pos = 0;
    bool header = true;
    while (pos < size)
    {
        usize end = pos;
        while (end < size && text[end] != '\n')
        {
            ++end;
        }
        if (header)
        {
            header = false;
            pos = end + 1;
            continue;
        }
        if (end == pos)
        {
            pos = end + 1;
            continue;
        }
        char line[256];
        if (end - pos >= sizeof(line))
        {
            return false;
        }
        for (usize k = 0; k < end - pos; ++k)
        {
            line[k] = text[pos + k];
        }
        line[end - pos] = '\0';
        // Split into seven fields at the commas.
        char* fields[7];
        usize count = 0;
        fields[count++] = line;
        for (char* c = line; *c != '\0'; ++c)
        {
            if (*c == ',')
            {
                *c = '\0';
                if (count == 7)
                {
                    return false;
                }
                fields[count++] = c + 1;
            }
        }
        if (count != 7)
        {
            return false;
        }
        Row row;
        FormatTo(row.scenario, sizeof(row.scenario), "%s", fields[0]);
        FormatTo(row.impl, sizeof(row.impl), "%s", fields[1]);
        row.entities = static_cast<u64>(CStrToLongLong(fields[2], nullptr, 10));
        row.participants = static_cast<u64>(CStrToLongLong(fields[3], nullptr, 10));
        row.median_ms = CStrToDouble(fields[4], nullptr);
        row.p95_ms = CStrToDouble(fields[5], nullptr);
        row.checksum = CStrToDouble(fields[6], nullptr);
        rows->Push(row);
        pos = end + 1;
    }
    return true;
}

}  // namespace ecs_bench
