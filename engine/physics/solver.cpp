#include "physics/solver.h"

#include "algorithm.h"
#include "containers/span.h"
#include "lang/scalar.h"
#include "physics/world.h"

namespace engine_placeholder::physics
{

namespace
{

constexpr f32 kWarmDistance = 0.05f;

Vec3 Perpendicular(const Vec3& n)
{
    // The axis least aligned with n, crossed with it.
    const Vec3 axis = Abs(n.x) <= Abs(n.y) && Abs(n.x) <= Abs(n.z) ? Vec3(1.0f, 0.0f, 0.0f)
                      : Abs(n.y) <= Abs(n.z)                       ? Vec3(0.0f, 1.0f, 0.0f)
                                                                   : Vec3(0.0f, 0.0f, 1.0f);
    const Vec3 t = n.Cross(axis);
    return t * (1.0f / Sqrt(t.LengthSquared()));
}

}  // namespace

struct ContactSolver::Row
{
    Vec3 ra = Vec3(0.0f, 0.0f, 0.0f);
    Vec3 rb = Vec3(0.0f, 0.0f, 0.0f);
    f32 normal_mass = 0.0f;
    f32 tangent_mass[2] = {0.0f, 0.0f};
    f32 target = 0.0f;
    f32 normal_impulse = 0.0f;
    f32 tangent_impulse[2] = {0.0f, 0.0f};
    u32 feature = 0;
    Vec3 local_a = Vec3(0.0f, 0.0f, 0.0f);
};

struct ContactSolver::Constraint
{
    ColliderId a;
    ColliderId b;
    Body* body_a = nullptr;
    Body* body_b = nullptr;
    Vec3 normal = Vec3(0.0f, 1.0f, 0.0f);
    Vec3 tangent[2];
    f32 friction = 0.0f;
    u32 count = 0;
    Row rows[4];
};

namespace
{

// The motion of a body in the solver: inverse mass and inverse inertia are zero unless the body is dynamic.
struct Motion
{
    f32 inverse_mass = 0.0f;
    Mat3 inverse_inertia;
};

f32 InverseMassOf(const Body& b)
{
    return b.settings.type == BodyType::Dynamic ? b.inverse_mass : 0.0f;
}

Vec3 WorldInertia(const Body& b, const Vec3& v)
{
    return b.settings.type == BodyType::Dynamic ? b.inverse_inertia_world * v : Vec3(0.0f, 0.0f, 0.0f);
}

Vec3 VelocityAt(const Body& b, const Vec3& r)
{
    return b.linear_velocity + b.angular_velocity.Cross(r);
}

void Apply(Body* a, Body* b, const Vec3& ra, const Vec3& rb, const Vec3& impulse)
{
    if (a->settings.type == BodyType::Dynamic)
    {
        a->linear_velocity = a->linear_velocity - impulse * a->inverse_mass;
        a->angular_velocity = a->angular_velocity - WorldInertia(*a, ra.Cross(impulse));
    }
    if (b->settings.type == BodyType::Dynamic)
    {
        b->linear_velocity = b->linear_velocity + impulse * b->inverse_mass;
        b->angular_velocity = b->angular_velocity + WorldInertia(*b, rb.Cross(impulse));
    }
}

// 1 / (m_a^-1 + m_b^-1 + the angular terms along a direction).
f32 EffectiveMass(const Body& a, const Body& b, const Vec3& ra, const Vec3& rb, const Vec3& d)
{
    const Vec3 ca = ra.Cross(d);
    const Vec3 cb = rb.Cross(d);
    const f32 k = InverseMassOf(a) + InverseMassOf(b) + ca.Dot(WorldInertia(a, ca)) + cb.Dot(WorldInertia(b, cb));
    return k > 1e-12f ? 1.0f / k : 0.0f;
}

Vec3 CenterOfMass(const Body& b)
{
    return b.position + b.orientation.ToMat3() * b.mass.center;
}

}  // namespace

void ContactSolver::Solve(PhysicsWorld* world, Span<const ContactInput> inputs, const SolverConfig& config)
{
    stats_ = SolverStats();
    // The constraints in the order of their colliders, whatever the order of the input.
    Vector<u32> order;
    for (usize i = 0; i < inputs.Size(); ++i)
    {
        order.Push(static_cast<u32>(i));
    }
    const auto key_a = [&](u32 i)
    { return inputs[i].a.ToKey() < inputs[i].b.ToKey() ? inputs[i].a.ToKey() : inputs[i].b.ToKey(); };
    const auto key_b = [&](u32 i)
    { return inputs[i].a.ToKey() < inputs[i].b.ToKey() ? inputs[i].b.ToKey() : inputs[i].a.ToKey(); };
    Sort(Span<u32>(order.Data(), order.Size()),
         [&](u32 x, u32 y)
         {
             if (key_a(x) != key_a(y))
             {
                 return key_a(x) < key_a(y);
             }
             return key_b(x) < key_b(y);
         });
    Vector<Constraint> constraints;
    constraints.Reserve(order.Size());
    for (usize o = 0; o < order.Size(); ++o)
    {
        const ContactInput& in = inputs[order[o]];
        ContactManifold m = in.manifold;
        ColliderId a = in.a;
        ColliderId b = in.b;
        BodyId ba = in.body_a;
        BodyId bb = in.body_b;
        if (a.ToKey() > b.ToKey())
        {
            m.Flip();
            const ColliderId tc = a;
            a = b;
            b = tc;
            const BodyId tb = ba;
            ba = bb;
            bb = tb;
        }
        Body* body_a = world->Find(ba);
        Body* body_b = world->Find(bb);
        if (body_a == nullptr || body_b == nullptr || m.count == 0 || body_a == body_b)
        {
            continue;
        }
        Constraint c;
        c.a = a;
        c.b = b;
        c.body_a = body_a;
        c.body_b = body_b;
        c.normal = m.normal;
        c.friction = m.friction;
        c.count = m.count;
        const Vec3 com_a = CenterOfMass(*body_a);
        const Vec3 com_b = CenterOfMass(*body_b);
        const Quaternion inv_a = body_a->orientation.Conjugate();
        Vec3 mean(0.0f, 0.0f, 0.0f);
        for (u32 k = 0; k < m.count; ++k)
        {
            mean = mean + (m.points[k].position_a + m.points[k].position_b) * (0.5f / static_cast<f32>(m.count));
        }
        // The friction directions: along the sliding velocity when there is one, so the friction cone is round where
        // it matters, and a fixed perpendicular otherwise.
        const Vec3 rel = VelocityAt(*body_b, mean - com_b) - VelocityAt(*body_a, mean - com_a);
        const Vec3 slide = rel - m.normal * rel.Dot(m.normal);
        const f32 slide2 = slide.LengthSquared();
        c.tangent[0] = slide2 > 1e-8f ? slide * (1.0f / Sqrt(slide2)) : Perpendicular(m.normal);
        c.tangent[1] = m.normal.Cross(c.tangent[0]);
        for (u32 k = 0; k < m.count; ++k)
        {
            Row& row = c.rows[k];
            const ContactPoint& p = m.points[k];
            row.ra = p.position_a - com_a;
            row.rb = p.position_b - com_b;
            row.feature = p.feature_id;
            row.local_a = inv_a.RotateVector(p.position_a - body_a->position);
            row.normal_mass = EffectiveMass(*body_a, *body_b, row.ra, row.rb, m.normal);
            row.tangent_mass[0] = EffectiveMass(*body_a, *body_b, row.ra, row.rb, c.tangent[0]);
            row.tangent_mass[1] = EffectiveMass(*body_a, *body_b, row.ra, row.rb, c.tangent[1]);
            // The velocity the row wants along the normal: the way out of a penetration, the speed a gap allows, and
            // the bounce when the bodies meet fast enough.
            const f32 vn = (VelocityAt(*body_b, row.rb) - VelocityAt(*body_a, row.ra)).Dot(m.normal);
            const f32 closing = -vn;
            f32 target;
            if (p.depth >= 0.0f)
            {
                const f32 over = p.depth - config.slop;
                target = over > 0.0f ? config.bias * over / config.dt : 0.0f;
            }
            else
            {
                target = p.depth / config.dt;
            }
            if (m.restitution > 0.0f && closing > config.restitution_threshold && p.depth + closing * config.dt >= 0.0f)
            {
                target = Max(target, m.restitution * closing);
            }
            row.target = target;
        }
        stats_.points += m.count;
        ++stats_.manifolds;
        constraints.Push(c);
    }
    // Warm start, once every row has its target (the targets use the velocities before any impulse): the cached
    // impulses of the same points.
    for (usize ci = 0; ci < constraints.Size(); ++ci)
    {
        Constraint& c = constraints[ci];
        for (u32 k = 0; k < c.count; ++k)
        {
            Row& row = c.rows[k];
            usize lo = 0;
            usize hi = cache_.Size();
            const u64 ka = c.a.ToKey();
            const u64 kb = c.b.ToKey();
            while (lo < hi)
            {
                const usize mid = (lo + hi) / 2;
                const CacheEntry& e = cache_[mid];
                const bool less = e.key_a != ka ? e.key_a < ka
                                                : (e.key_b != kb ? e.key_b < kb : e.feature < row.feature);
                if (less)
                {
                    lo = mid + 1;
                }
                else
                {
                    hi = mid;
                }
            }
            if (lo < cache_.Size() && cache_[lo].key_a == ka && cache_[lo].key_b == kb &&
                cache_[lo].feature == row.feature)
            {
                const CacheEntry& e = cache_[lo];
                if (e.teleports_a == c.body_a->teleports && e.teleports_b == c.body_b->teleports &&
                    (e.local_a - row.local_a).LengthSquared() < kWarmDistance * kWarmDistance)
                {
                    row.normal_impulse = e.normal_impulse;
                    row.tangent_impulse[0] = e.tangent_impulse.Dot(c.tangent[0]);
                    row.tangent_impulse[1] = e.tangent_impulse.Dot(c.tangent[1]);
                    const Vec3 impulse = c.normal * row.normal_impulse + c.tangent[0] * row.tangent_impulse[0] +
                                         c.tangent[1] * row.tangent_impulse[1];
                    Apply(c.body_a, c.body_b, row.ra, row.rb, impulse);
                    ++stats_.warm_started;
                }
            }
        }
    }
    for (u32 iteration = 0; iteration < config.iterations; ++iteration)
    {
        for (usize ci = 0; ci < constraints.Size(); ++ci)
        {
            Constraint& c = constraints[ci];
            for (u32 k = 0; k < c.count; ++k)
            {
                Row& row = c.rows[k];
                // Friction first, within the cone of the current normal impulse.
                const f32 limit = c.friction * row.normal_impulse;
                for (u32 t = 0; t < 2; ++t)
                {
                    const f32 vt = (VelocityAt(*c.body_b, row.rb) - VelocityAt(*c.body_a, row.ra)).Dot(c.tangent[t]);
                    f32 lambda = -vt * row.tangent_mass[t];
                    const f32 next = Clamp(row.tangent_impulse[t] + lambda, -limit, limit);
                    lambda = next - row.tangent_impulse[t];
                    row.tangent_impulse[t] = next;
                    Apply(c.body_a, c.body_b, row.ra, row.rb, c.tangent[t] * lambda);
                }
                const f32 vn = (VelocityAt(*c.body_b, row.rb) - VelocityAt(*c.body_a, row.ra)).Dot(c.normal);
                f32 lambda = (row.target - vn) * row.normal_mass;
                const f32 next = Max(row.normal_impulse + lambda, 0.0f);
                lambda = next - row.normal_impulse;
                row.normal_impulse = next;
                Apply(c.body_a, c.body_b, row.ra, row.rb, c.normal * lambda);
            }
        }
    }
    // The new cache: this step's impulses, in key order.
    next_cache_.Clear();
    for (usize ci = 0; ci < constraints.Size(); ++ci)
    {
        const Constraint& c = constraints[ci];
        for (u32 k = 0; k < c.count; ++k)
        {
            CacheEntry e;
            e.key_a = c.a.ToKey();
            e.key_b = c.b.ToKey();
            e.feature = c.rows[k].feature;
            e.body_a = c.body_a->id;
            e.body_b = c.body_b->id;
            e.teleports_a = c.body_a->teleports;
            e.teleports_b = c.body_b->teleports;
            e.local_a = c.rows[k].local_a;
            e.normal_impulse = c.rows[k].normal_impulse;
            e.tangent_impulse = c.tangent[0] * c.rows[k].tangent_impulse[0] +
                                c.tangent[1] * c.rows[k].tangent_impulse[1];
            next_cache_.Push(e);
        }
    }
    Sort(Span<CacheEntry>(next_cache_.Data(), next_cache_.Size()),
         [](const CacheEntry& x, const CacheEntry& y)
         {
             if (x.key_a != y.key_a)
             {
                 return x.key_a < y.key_a;
             }
             if (x.key_b != y.key_b)
             {
                 return x.key_b < y.key_b;
             }
             return x.feature < y.feature;
         });
    cache_.Clear();
    for (usize i = 0; i < next_cache_.Size(); ++i)
    {
        cache_.Push(next_cache_[i]);
    }
}

void ContactSolver::ForgetBody(BodyId body)
{
    next_cache_.Clear();
    for (usize i = 0; i < cache_.Size(); ++i)
    {
        const CacheEntry& e = cache_[i];
        const bool mine = (e.body_a.index == body.index && e.body_a.generation == body.generation) ||
                          (e.body_b.index == body.index && e.body_b.generation == body.generation);
        if (!mine)
        {
            next_cache_.Push(e);
        }
    }
    cache_.Clear();
    for (usize i = 0; i < next_cache_.Size(); ++i)
    {
        cache_.Push(next_cache_[i]);
    }
}

void ContactSolver::Clear()
{
    cache_.Clear();
}

}  // namespace engine_placeholder::physics
