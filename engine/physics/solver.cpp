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
    Vec3 ra = Vec3(0.0f, 0.0f, 0.0f);  // anchors from the centres of mass, fixed for the step
    Vec3 rb = Vec3(0.0f, 0.0f, 0.0f);
    f32 normal_mass = 0.0f;
    f32 tangent_mass[2] = {0.0f, 0.0f};
    f32 relative_velocity = 0.0f;  // along the normal before the solve, for the restitution
    f32 normal_impulse = 0.0f;
    f32 max_normal_impulse = 0.0f;
    f32 tangent_impulse[2] = {0.0f, 0.0f};
    f32 depth = 0.0f;
    u32 feature = 0;
    Vec3 local_a = Vec3(0.0f, 0.0f, 0.0f);
};

// What a substep knows of a body: where it started and how far it has moved since.
struct ContactSolver::BodyState
{
    Body* body = nullptr;
    Quaternion q0 = Quaternion::Identity();
    Vec3 com0 = Vec3(0.0f, 0.0f, 0.0f);
    Vec3 dp = Vec3(0.0f, 0.0f, 0.0f);
    Quaternion dq = Quaternion::Identity();
    bool moves = false;
};

struct ContactSolver::Constraint
{
    ColliderId a;
    ColliderId b;
    u32 sa = 0;
    u32 sb = 0;
    Vec3 normal = Vec3(0.0f, 1.0f, 0.0f);
    Vec3 tangent[2];
    f32 friction = 0.0f;
    f32 restitution = 0.0f;
    u32 count = 0;
    f32 bias_rate = 0.0f;
    f32 mass_scale = 1.0f;
    f32 impulse_scale = 0.0f;
    Row rows[4];
};

namespace
{

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

// The soft constraint of a spring of `hertz` and `ratio` solved over `h` (Box2D's formulation: the constraint reacts
// as a damped spring, and the same numbers work for any stiffness without a stability limit).
void MakeSoft(f32 hertz, f32 ratio, f32 h, f32* bias_rate, f32* mass_scale, f32* impulse_scale)
{
    constexpr f32 kTwoPi = 6.2831853f;
    if (hertz <= 0.0f)
    {
        *bias_rate = 0.0f;
        *mass_scale = 1.0f;
        *impulse_scale = 0.0f;
        return;
    }
    const f32 omega = kTwoPi * hertz;
    const f32 a1 = 2.0f * ratio + h * omega;
    const f32 a2 = h * omega * a1;
    const f32 a3 = 1.0f / (1.0f + a2);
    *bias_rate = omega / a1;
    *mass_scale = a2 * a3;
    *impulse_scale = a3;
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
    const u32 substeps = config.substeps < 1 ? 1 : (config.substeps > 16 ? 16 : config.substeps);
    const f32 h = config.dt / static_cast<f32>(substeps);
    const f32 inv_h = 1.0f / h;
    // The spring may not be stiffer than a quarter of the substep rate.
    const f32 hertz = Min(config.contact_hertz, 0.25f * inv_h);
    f32 dynamic_bias, dynamic_mass_scale, dynamic_impulse_scale;
    f32 static_bias, static_mass_scale, static_impulse_scale;
    MakeSoft(hertz, config.damping_ratio, h, &dynamic_bias, &dynamic_mass_scale, &dynamic_impulse_scale);
    MakeSoft(2.0f * hertz, config.damping_ratio, h, &static_bias, &static_mass_scale, &static_impulse_scale);
    // The bodies of the constraints, one state each.
    Vector<BodyState> states;
    Vector<i32> state_of;
    const u32 slots = world->Stats().body_slots;
    for (u32 i = 0; i < slots; ++i)
    {
        state_of.Push(-1);
    }
    const auto state_for = [&](Body* body) -> u32
    {
        i32& index = state_of[body->id.index];
        if (index < 0)
        {
            index = static_cast<i32>(states.Size());
            BodyState st;
            st.body = body;
            st.q0 = body->orientation;
            st.com0 = CenterOfMass(*body);
            st.moves = body->settings.type != BodyType::Static;
            states.Push(st);
        }
        return static_cast<u32>(index);
    };
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
        c.sa = state_for(body_a);
        c.sb = state_for(body_b);
        c.normal = m.normal;
        c.friction = m.friction;
        c.restitution = m.restitution;
        c.count = m.count;
        const bool with_static = body_a->settings.type != BodyType::Dynamic ||
                                 body_b->settings.type != BodyType::Dynamic;
        c.bias_rate = with_static ? static_bias : dynamic_bias;
        c.mass_scale = with_static ? static_mass_scale : dynamic_mass_scale;
        c.impulse_scale = with_static ? static_impulse_scale : dynamic_impulse_scale;
        const Vec3 com_a = states[c.sa].com0;
        const Vec3 com_b = states[c.sb].com0;
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
            row.depth = p.depth;
            row.local_a = inv_a.RotateVector(p.position_a - body_a->position);
            row.normal_mass = EffectiveMass(*body_a, *body_b, row.ra, row.rb, m.normal);
            row.tangent_mass[0] = EffectiveMass(*body_a, *body_b, row.ra, row.rb, c.tangent[0]);
            row.tangent_mass[1] = EffectiveMass(*body_a, *body_b, row.ra, row.rb, c.tangent[1]);
            row.relative_velocity = (VelocityAt(*body_b, row.rb) - VelocityAt(*body_a, row.ra)).Dot(m.normal);
        }
        stats_.points += m.count;
        ++stats_.manifolds;
        constraints.Push(c);
    }
    // The cached impulses of the same points start the rows.
    for (usize ci = 0; ci < constraints.Size(); ++ci)
    {
        Constraint& c = constraints[ci];
        const Body& body_a = *states[c.sa].body;
        const Body& body_b = *states[c.sb].body;
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
                if (e.teleports_a == body_a.teleports && e.teleports_b == body_b.teleports &&
                    (e.local_a - row.local_a).LengthSquared() < kWarmDistance * kWarmDistance)
                {
                    row.normal_impulse = e.normal_impulse;
                    row.tangent_impulse[0] = e.tangent_impulse.Dot(c.tangent[0]);
                    row.tangent_impulse[1] = e.tangent_impulse.Dot(c.tangent[1]);
                    ++stats_.warm_started;
                }
            }
        }
    }
    // One pass over every constraint: friction first, then the normal row (non-penetration matters more, so it comes
    // last). `use_bias` is the position feedback; the relax pass runs without it.
    const auto solve = [&](bool use_bias, bool backward)
    {
        for (usize cj = 0; cj < constraints.Size(); ++cj)
        {
            Constraint& c = constraints[backward ? constraints.Size() - 1 - cj : cj];
            BodyState& sa = states[c.sa];
            BodyState& sb = states[c.sb];
            Body* a = sa.body;
            Body* b = sb.body;
            const Vec3 dp = sb.dp - sa.dp;
            for (u32 kj = 0; kj < c.count; ++kj)
            {
                const u32 k = backward ? c.count - 1 - kj : kj;
                Row& row = c.rows[k];
                // Where the points are now: the start separation, the bodies' motion and the turn of the anchors.
                const Vec3 prel = dp + (sb.dq.RotateVector(row.rb) - row.rb) - (sa.dq.RotateVector(row.ra) - row.ra);
                const f32 s = prel.Dot(c.normal) - row.depth;
                // Friction.
                const f32 limit = c.friction * row.normal_impulse;
                for (u32 t = 0; t < 2; ++t)
                {
                    const f32 vt = (VelocityAt(*b, row.rb) - VelocityAt(*a, row.ra)).Dot(c.tangent[t]);
                    f32 lambda = -vt * row.tangent_mass[t];
                    const f32 next = Clamp(row.tangent_impulse[t] + lambda, -limit, limit);
                    lambda = next - row.tangent_impulse[t];
                    row.tangent_impulse[t] = next;
                    Apply(a, b, row.ra, row.rb, c.tangent[t] * lambda);
                }
                // The normal row: a gap is closed in the time that remains, an overlap by the soft constraint.
                f32 bias = 0.0f;
                f32 mass_scale = 1.0f;
                f32 impulse_scale = 0.0f;
                if (s > 0.0f)
                {
                    bias = s * inv_h;
                }
                else if (use_bias)
                {
                    bias = Max(c.bias_rate * Min(s + config.slop, 0.0f), -config.push_velocity);
                    mass_scale = c.mass_scale;
                    impulse_scale = c.impulse_scale;
                }
                const f32 vn = (VelocityAt(*b, row.rb) - VelocityAt(*a, row.ra)).Dot(c.normal);
                f32 lambda = -row.normal_mass * mass_scale * (vn + bias) - impulse_scale * row.normal_impulse;
                const f32 next = Max(row.normal_impulse + lambda, 0.0f);
                lambda = next - row.normal_impulse;
                row.normal_impulse = next;
                row.max_normal_impulse = Max(row.max_normal_impulse, next);
                Apply(a, b, row.ra, row.rb, c.normal * lambda);
            }
        }
    };
    const auto warm_start = [&]()
    {
        for (usize ci = 0; ci < constraints.Size(); ++ci)
        {
            Constraint& c = constraints[ci];
            for (u32 k = 0; k < c.count; ++k)
            {
                const Row& row = c.rows[k];
                const Vec3 impulse = c.normal * row.normal_impulse + c.tangent[0] * row.tangent_impulse[0] +
                                     c.tangent[1] * row.tangent_impulse[1];
                Apply(states[c.sa].body, states[c.sb].body, row.ra, row.rb, impulse);
            }
        }
    };
    for (u32 sub = 0; sub < substeps; ++sub)
    {
        for (usize i = 0; i < states.Size(); ++i)
        {
            if (states[i].moves)
            {
                PhysicsWorld::IntegrateVelocity(config.gravity, states[i].body, h);
            }
        }
        warm_start();
        for (u32 it = 0; it < config.iterations; ++it)
        {
            solve(true, ((sub + it) & 1u) != 0);
        }
        for (usize i = 0; i < states.Size(); ++i)
        {
            BodyState& st = states[i];
            if (!st.moves)
            {
                continue;
            }
            PhysicsWorld::LimitVelocity(config.max_velocity, st.body);
            PhysicsWorld::IntegratePosition(st.body, h);
            st.dp = CenterOfMass(*st.body) - st.com0;
            st.dq = st.body->orientation * st.q0.Conjugate();
        }
        for (u32 it = 0; it < config.iterations; ++it)
        {
            solve(false, ((sub + it) & 1u) != 0);
        }
    }
    // Restitution: the points that met fast enough bounce with e times their approach speed.
    for (usize ci = 0; ci < constraints.Size(); ++ci)
    {
        Constraint& c = constraints[ci];
        if (c.restitution <= 0.0f)
        {
            continue;
        }
        Body* a = states[c.sa].body;
        Body* b = states[c.sb].body;
        for (u32 k = 0; k < c.count; ++k)
        {
            Row& row = c.rows[k];
            if (row.relative_velocity > -config.restitution_threshold || row.max_normal_impulse == 0.0f)
            {
                continue;
            }
            const f32 vn = (VelocityAt(*b, row.rb) - VelocityAt(*a, row.ra)).Dot(c.normal);
            f32 lambda = -row.normal_mass * (vn + c.restitution * row.relative_velocity);
            const f32 next = Max(row.normal_impulse + lambda, 0.0f);
            lambda = next - row.normal_impulse;
            row.normal_impulse = next;
            Apply(a, b, row.ra, row.rb, c.normal * lambda);
        }
    }
    for (usize i = 0; i < states.Size(); ++i)
    {
        if (states[i].moves)
        {
            states[i].body->integrated = true;
        }
    }
    stats_.iterations = constraints.Size() > 0 ? substeps * config.iterations : 0;
    for (usize ci = 0; ci < constraints.Size(); ++ci)
    {
        for (u32 k = 0; k < constraints[ci].count; ++k)
        {
            stats_.max_penetration = Max(stats_.max_penetration, constraints[ci].rows[k].depth);
            stats_.max_impulse = Max(stats_.max_impulse, constraints[ci].rows[k].max_normal_impulse);
        }
    }
    // The new cache: this step's impulses, in key order. The anchor is kept in body A's frame at the step's start.
    next_cache_.Clear();
    for (usize ci = 0; ci < constraints.Size(); ++ci)
    {
        const Constraint& c = constraints[ci];
        const Body* body_a = states[c.sa].body;
        const Body* body_b = states[c.sb].body;
        for (u32 k = 0; k < c.count; ++k)
        {
            CacheEntry e;
            e.key_a = c.a.ToKey();
            e.key_b = c.b.ToKey();
            e.feature = c.rows[k].feature;
            e.body_a = body_a->id;
            e.body_b = body_b->id;
            e.teleports_a = body_a->teleports;
            e.teleports_b = body_b->teleports;
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
