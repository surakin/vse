// The fixed-step driver and the integration (Phase 25 Day 390). Everything here uses only + - * / and Sqrt
// (docs/guide/physics.md, "The rules of the step").
#include "containers/hash_set.h"
#include "int128.h"
#include "jobs/parallel_for.h"
#include "lang/scalar.h"
#include "log/assert.h"
#include "os/os.h"
#include "physics/broad_phase.h"
#include "physics/broad_phase.h"
#include "physics/check.h"
#include "physics/collide.h"
#include "physics/collide.h"
#include "physics/determinism.h"
#include "physics/query_index.h"
#include "physics/state_hash.h"
#include "physics/world.h"

namespace vse::physics
{

namespace
{

// The fastest a body may spin, in radians per second.
constexpr f32 kMaxSpin = 100.0f;

// Turns q by the angular velocity w over dt: q' = normalize(q + dt/2 * (w, 0) * q).
Quaternion Turn(const Quaternion& q, const Vec3& w, f32 dt)
{
    const Quaternion spin(w.x, w.y, w.z, 0.0f);
    const Quaternion delta = spin * q;
    const f32 half = 0.5f * dt;
    const Quaternion turned(q.x + half * delta.x, q.y + half * delta.y, q.z + half * delta.z, q.w + half * delta.w);
    // A speed so large that the squares overflow (a finite host input) must not abort the step: the body keeps its
    // orientation.
    const f32 n2 = turned.LengthSquared();
    if (!IsFinite(n2) || n2 < 1e-30f)
    {
        return q;
    }
    return turned.Normalized();
}

// Runs f(begin, end) over [0, count) in blocks of a fixed size: inline, or as jobs. The blocks are the same either way
// and each writes only its own range, so the result does not depend on how many threads run them.
template <class F> void ForBlocks(jobs::Scheduler* scheduler, usize count, usize block, F& f)
{
    if (scheduler == nullptr || count <= block)
    {
        if (count != 0)
        {
            f(usize{0}, count);
        }
        return;
    }
    jobs::ParallelForBlocking(*scheduler, count, block, f);
}

}  // namespace

PhysicsWorld::PhysicsWorld() = default;
PhysicsWorld::~PhysicsWorld() = default;
PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

BodyStatus PhysicsWorld::SetStepConfig(const StepConfig& config)
{
    if (!IsFinite(config.step_hz) || config.step_hz < 1.0f || config.step_hz > 1000.0f || config.max_substeps < 1 ||
        config.max_substeps > 32 || !IsFinite(config.contact_margin) || config.contact_margin < 0.0f ||
        config.contact_margin > 1.0f || !FiniteVec3(config.gravity) || config.velocity_iterations < 1 ||
        config.velocity_iterations > 100 || config.substeps < 1 || config.substeps > 16 ||
        !IsFinite(config.contact_slop) || config.contact_slop < 0.0f || config.contact_slop > 0.1f ||
        !IsFinite(config.restitution_threshold) || config.restitution_threshold < 0.0f ||
        !IsFinite(config.contact_hertz) || config.contact_hertz < 0.0f || config.contact_hertz > 1000.0f ||
        !IsFinite(config.contact_damping_ratio) || config.contact_damping_ratio < 0.0f ||
        config.contact_damping_ratio > 1000.0f || !IsFinite(config.contact_push_velocity) ||
        config.contact_push_velocity < 0.0f || !IsFinite(config.max_velocity) || config.max_velocity < 1.0f ||
        !IsFinite(config.joint_hertz) || config.joint_hertz < 0.0f || config.joint_hertz > 1000.0f ||
        !IsFinite(config.joint_damping_ratio) || config.joint_damping_ratio < 0.0f ||
        config.joint_damping_ratio > 1000.0f || !IsFinite(config.sleep_linear) || config.sleep_linear < 0.0f ||
        !IsFinite(config.sleep_angular) || config.sleep_angular < 0.0f || !IsFinite(config.sleep_time) ||
        config.sleep_time < 0.0f || config.threads < 1 || config.threads > 256 || !IsFinite(config.ccd_threshold) ||
        config.ccd_threshold < 0.0f || config.ccd_threshold > 100.0f)
    {
        return BodyStatus::Invalid;
    }
    if (config.step_hz != step_config_.step_hz || config.ccd_threshold != step_config_.ccd_threshold)
    {
        broad_.Reset();  // its prediction time follows the step
    }
    step_config_ = config;
    return BodyStatus::Ok;
}

void PhysicsWorld::IntegrateVelocity(const Vec3& gravity, Body* body, f32 dt)
{
    if (body->settings.type != BodyType::Dynamic)
    {
        return;
    }
    // Forces and gravity, then damping (v / (1 + c dt), stable for any c dt).
    const Vec3 acceleration = gravity * body->settings.gravity_scale + body->force * body->inverse_mass;
    body->linear_velocity = body->linear_velocity + acceleration * dt;
    body->angular_velocity = body->angular_velocity + (body->inverse_inertia_world * body->torque) * dt;
    body->linear_velocity = body->linear_velocity * (1.0f / (1.0f + body->settings.linear_damping * dt));
    body->angular_velocity = body->angular_velocity * (1.0f / (1.0f + body->settings.angular_damping * dt));
}

void PhysicsWorld::LimitVelocity(f32 max_velocity, Body* body)
{
    if (body->settings.type != BodyType::Dynamic)
    {
        return;
    }
    const f32 speed2 = body->linear_velocity.LengthSquared();
    if (speed2 > max_velocity * max_velocity)
    {
        body->linear_velocity = body->linear_velocity * (max_velocity / Sqrt(speed2));
    }
    const f32 spin2 = body->angular_velocity.LengthSquared();
    if (spin2 > kMaxSpin * kMaxSpin)
    {
        body->angular_velocity = body->angular_velocity * (kMaxSpin / Sqrt(spin2));
    }
}

void PhysicsWorld::IntegratePosition(Body* body, f32 dt)
{
    if (body->settings.type == BodyType::Dynamic)
    {
        // The centre of mass moves by the new velocity, the body turns about it.
        const Vec3 offset = body->orientation.ToMat3() * body->mass.center;
        const Vec3d center = body->position + Vec3d(offset) + Vec3d(body->linear_velocity * dt);
        body->orientation = Turn(body->orientation, body->angular_velocity, dt);
        body->position = center - Vec3d(body->orientation.ToMat3() * body->mass.center);
        RefreshInertia(body);
    }
    else if (body->settings.type == BodyType::Kinematic)
    {
        // A kinematic body is moved by its velocity, about its origin; nothing pushes it.
        body->position = body->position + Vec3d(body->linear_velocity * dt);
        body->orientation = Turn(body->orientation, body->angular_velocity, dt);
    }
}

f32 PhysicsWorld::Alpha() const
{
    const f64 step = 1.0 / static_cast<f64>(step_config_.step_hz);
    const f64 alpha = accumulator_ / step;
    return static_cast<f32>(alpha < 0.0 ? 0.0 : (alpha > 1.0 ? 1.0 : alpha));
}

u32 PhysicsWorld::Update(f32 real_seconds)
{
    step_stats_.last_substeps = 0;
    if (!IsFinite(real_seconds) || real_seconds < 0.0f)
    {
        return 0;
    }
    const f64 step = 1.0 / static_cast<f64>(step_config_.step_hz);
    accumulator_ += static_cast<f64>(real_seconds);
    u32 ran = 0;
    ClearEvents();
    keep_events_ = true;
    while (accumulator_ >= step && ran < step_config_.max_substeps)
    {
        Step();
        accumulator_ -= step;
        ++ran;
    }
    if (accumulator_ >= step)
    {
        // More time than the cap can use: keep only the part of a step, drop the rest.
        const f64 kept = accumulator_ - Floor(accumulator_ / step) * step;
        step_stats_.dropped_seconds += accumulator_ - kept;
        ++step_stats_.dropped_updates;
        accumulator_ = kept;
    }
    keep_events_ = false;
    step_stats_.last_substeps = ran;
    return ran;
}

void PhysicsWorld::Step()
{
    // The rules of the step hold only if the process keeps denormals; a host that set flush-to-zero gets a report and a
    // counter, not a silent difference.
    if (!keep_events_)
    {
        ClearEvents();
    }
    const bool denormals_kept = DenormalsPreserved();
    if (!ENGINE_ENSURE(denormals_kept,
                       "the physics step runs with flush-to-zero or denormals-are-zero set: results are not portable"))
    {
        ++step_stats_.denormal_mode_steps;
    }
    const f32 dt = static_cast<f32>(1.0 / static_cast<f64>(step_config_.step_hz));
    if (pre_step_ != nullptr)
    {
        pre_step_(pre_step_user_, this, dt);
    }
    const u32 slots = bodies_.Slots();
    // The previous poses are kept for the interpolation.
    for (u32 slot = 0; slot < slots; ++slot)
    {
        Body* body = bodies_.Find(slot, bodies_.LiveGeneration(slot));
        if (body != nullptr && body->settings.type != BodyType::Static && !body->sleeping)
        {
            body->previous_position = body->position;
            body->previous_orientation = body->orientation;
        }
    }
    // Contacts: the broad phase's pairs and the narrow phase, on the poses the step starts from.
    if (broad_.Get() == nullptr)
    {
        BroadPhaseConfig broad_config;
        broad_config.step_seconds = dt;
        broad_config.ccd_threshold = step_config_.ccd_threshold;
        broad_ = MakeUnique<BroadPhase>(broad_config);
    }
    const u64 broad_start = os::MonotonicTimeNanos();
    broad_->Update(*this);
    const u64 narrow_start = os::MonotonicTimeNanos();
    step_stats_.broad_ns += narrow_start - broad_start;
    // A sleeping body wakes when a joint ties it to something that moves.
    for (u32 slot = 0; slot < joints_.Slots(); ++slot)
    {
        const JointRecord* joint = joints_.Find(slot, joints_.LiveGeneration(slot));
        if (joint == nullptr)
        {
            continue;
        }
        Body* ja = joint->desc.body_a.IsValid() ? Find(joint->desc.body_a) : nullptr;
        Body* jb = joint->desc.body_b.IsValid() ? Find(joint->desc.body_b) : nullptr;
        if (ja != nullptr && jb != nullptr)
        {
            if (ja->sleeping && Disturbing(*jb))
            {
                WakeIsland(ja);
            }
            if (jb->sleeping && Disturbing(*ja))
            {
                WakeIsland(jb);
            }
        }
    }
    // The body pairs of joints that keep their bodies from colliding, as a set of (low id, high id) keys: membership
    // only, so the order of the pairs and of the work is not touched (container audit Day 7: every broad-phase pair
    // scanned every joint).
    HashSet<u128, HeapAllocator> connected(HeapAllocator::Default());
    const auto pair_key = [](u64 x, u64 y) { return x < y ? (u128(x) << 64) | u128(y) : (u128(y) << 64) | u128(x); };
    for (u32 slot = 0; slot < joints_.Slots(); ++slot)
    {
        const JointRecord* joint = joints_.Find(slot, joints_.LiveGeneration(slot));
        if (joint != nullptr && !joint->desc.collide_connected && joint->desc.body_a.IsValid() &&
            joint->desc.body_b.IsValid())
        {
            connected.Insert(pair_key(joint->desc.body_a.ToU64(), joint->desc.body_b.ToU64()));
        }
    }
    Vector<ContactInput> inputs;
    // The ends of the contacts between bodies that sleep or move, as slot pairs; a body paired with itself touches
    // something that moves, which keeps its island awake.
    Vector<u32> edges;
    Vector<usize> idle;  // pairs of two bodies that rest: a contact wakes one, so they are looked at again after
    Vector<ColliderPair> carried;  // pairs of bodies that kept resting: their contact state is what it was
    Vector<ColliderPair> trigger_pairs;
    const Vector<ColliderPair>& pairs = broad_->Pairs();
    jobs::Scheduler* scheduler = step_config_.threads > 1 ? scheduler_ : nullptr;
    // One pair that needs the narrow phase: the narrow phase fills `input` and `touching` from the bodies' poses (a
    // read-only job), then `apply` below uses the result on the main thread in pair order.
    struct PairWork
    {
        const ColliderRecord* ra;
        const ColliderRecord* rb;
        Body* ba;
        Body* bb;
        ColliderPair pair;
        ContactInput input;
        bool touching;
    };
    Vector<PairWork> work;
    const auto classify = [&](const ColliderPair& pair, bool first, usize index)
    {
        if (pair.trigger)
        {
            if (first)
            {
                trigger_pairs.Push(pair);
            }
            return;
        }
        const ColliderRecord* ra = FindCollider(pair.a);
        const ColliderRecord* rb = FindCollider(pair.b);
        if (ra == nullptr || rb == nullptr || ra->owner == rb->owner)
        {
            return;
        }
        Body* ba = Find(ra->owner);
        Body* bb = Find(rb->owner);
        if (ba == nullptr || bb == nullptr ||
            (ba->settings.type != BodyType::Dynamic && bb->settings.type != BodyType::Dynamic))
        {
            return;
        }
        if (!Disturbing(*ba) && !Disturbing(*bb))
        {
            if (first)
            {
                idle.Push(index);
            }
            else
            {
                carried.Push(pair);
            }
            return;
        }
        if (!connected.Empty() && connected.Contains(pair_key(ra->owner.ToU64(), rb->owner.ToU64())))
        {
            return;
        }
        PairWork item;
        item.ra = ra;
        item.rb = rb;
        item.ba = ba;
        item.bb = bb;
        item.pair = pair;
        item.touching = false;
        work.Push(item);
    };
    auto collide_block = [&](usize begin, usize end)
    {
        for (usize k = begin; k < end; ++k)
        {
            PairWork& w = work[k];
            // Both shapes are placed relative to the first body's origin (the difference taken in double precision),
            // and the manifold keeps that origin as its anchor.
            const Vec3d anchor = w.ba->position;
            const Pose pose_a = PoseFrom(*w.ba, w.ra->value, anchor);
            const Pose pose_b = PoseFrom(*w.bb, w.rb->value, anchor);
            w.touching = Collide(w.ra->value.shape, pose_a, w.rb->value.shape, pose_b, step_config_.contact_margin,
                                 &w.input.manifold, &hulls_, &meshes_, &compounds_,
                                 &height_fields_) == CollideResult::Contact;
            w.input.manifold.anchor = anchor;
        }
    };
    // Collides the queued pairs (in blocks of 32, whatever the thread count), then applies the contacts in order: a
    // touch wakes what sleeps and adds the island edge.
    const auto run_work = [&]
    {
        ForBlocks(scheduler, work.Size(), 32, collide_block);
        for (usize k = 0; k < work.Size(); ++k)
        {
            PairWork& w = work[k];
            if (!w.touching)
            {
                continue;
            }
            ContactInput& input = w.input;
            CombineMaterials(w.ra->value.material, w.rb->value.material, &input.manifold);
            input.a = w.pair.a;
            input.b = w.pair.b;
            input.body_a = w.ra->owner;
            input.body_b = w.rb->owner;
            const bool both_dynamic = w.ba->settings.type == BodyType::Dynamic &&
                                      w.bb->settings.type == BodyType::Dynamic;
            if (w.ba->sleeping)
            {
                WakeIsland(w.ba);
            }
            if (w.bb->sleeping)
            {
                WakeIsland(w.bb);
            }
            // A contact deeper than a tenth of a metre is being pushed apart (a body teleported into the ground comes
            // out at the push velocity, which the relaxation leaves no speed of): neither body sleeps meanwhile.
            f32 deepest = 0.0f;
            for (u32 p = 0; p < input.manifold.count; ++p)
            {
                deepest = FMax(deepest, input.manifold.points[p].depth);
            }
            if (deepest > 0.1f)
            {
                if (w.ba->settings.type == BodyType::Dynamic)
                {
                    edges.Push(w.ba->id.index);
                    edges.Push(w.ba->id.index);
                }
                if (w.bb->settings.type == BodyType::Dynamic)
                {
                    edges.Push(w.bb->id.index);
                    edges.Push(w.bb->id.index);
                }
            }
            if (both_dynamic)
            {
                edges.Push(w.ba->id.index);
                edges.Push(w.bb->id.index);
            }
            else
            {
                const Body* mover = w.ba->settings.type == BodyType::Dynamic ? w.bb : w.ba;
                const Body* mine = w.ba->settings.type == BodyType::Dynamic ? w.ba : w.bb;
                if (Disturbing(*mover))
                {
                    edges.Push(mine->id.index);
                    edges.Push(mine->id.index);
                }
            }
            inputs.Push(input);
        }
        work.Clear();
    };
    for (usize i = 0; i < pairs.Size(); ++i)
    {
        classify(pairs[i], true, i);
    }
    run_work();
    // The pairs of bodies that were asleep and have just been woken.
    for (usize i = 0; i < idle.Size(); ++i)
    {
        classify(pairs[idle[i]], false, idle[i]);
    }
    run_work();
    UpdateTriggerEvents(trigger_pairs);
    // The bodies that touch take their substeps in the solver; the rest take one step.
    SolverConfig solver_config;
    solver_config.substeps = step_config_.substeps;
    solver_config.iterations = step_config_.velocity_iterations;
    solver_config.contact_hertz = step_config_.contact_hertz;
    solver_config.damping_ratio = step_config_.contact_damping_ratio;
    solver_config.push_velocity = step_config_.contact_push_velocity;
    solver_config.slop = step_config_.contact_slop;
    solver_config.restitution_threshold = step_config_.restitution_threshold;
    solver_config.dt = dt;
    solver_config.gravity = step_config_.gravity;
    solver_config.max_velocity = step_config_.max_velocity;
    solver_config.joint_hertz = step_config_.joint_hertz;
    solver_config.joint_damping_ratio = step_config_.joint_damping_ratio;
    const u64 solve_start = os::MonotonicTimeNanos();
    step_stats_.narrow_ns += solve_start - narrow_start;
    solver_.Solve(this, Span<const ContactInput>(inputs.Data(), inputs.Size()), solver_config);
    const u64 integrate_start = os::MonotonicTimeNanos();
    step_stats_.solve_ns += integrate_start - solve_start;
    // The bodies the solver did not integrate (free bodies, kinematic bodies) take their plain step; they are
    // independent, so blocks of them run as jobs.
    Vector<Body*> free_bodies;
    for (u32 slot = 0; slot < slots; ++slot)
    {
        Body* body = bodies_.Find(slot, bodies_.LiveGeneration(slot));
        if (body == nullptr || body->settings.type == BodyType::Static)
        {
            continue;
        }
        if (!body->sleeping && !body->integrated)
        {
            free_bodies.Push(body);
        }
        body->integrated = false;
    }
    auto integrate_block = [&](usize begin, usize end)
    {
        for (usize k = begin; k < end; ++k)
        {
            Body* body = free_bodies[k];
            IntegrateVelocity(step_config_.gravity, body, dt);
            LimitVelocity(step_config_.max_velocity, body);
            IntegratePosition(body, dt);
            body->force = Vec3(0.0f, 0.0f, 0.0f);
            body->torque = Vec3(0.0f, 0.0f, 0.0f);
        }
    };
    ForBlocks(scheduler, free_bodies.Size(), 256, integrate_block);
    // The solver's bodies and the sleeping ones only lose the forces applied since the last step.
    for (u32 slot = 0; slot < slots; ++slot)
    {
        Body* body = bodies_.Find(slot, bodies_.LiveGeneration(slot));
        if (body != nullptr && body->settings.type != BodyType::Static)
        {
            body->force = Vec3(0.0f, 0.0f, 0.0f);
            body->torque = Vec3(0.0f, 0.0f, 0.0f);
        }
    }
    const u64 after_start = os::MonotonicTimeNanos();
    step_stats_.integrate_ns += after_start - integrate_start;
    UpdateContactEvents(inputs, carried);
    SweepContinuous();
    UpdateSleep(edges, dt);
    for (u32 slot = 0; slot < slots; ++slot)
    {
        Body* body = bodies_.Find(slot, bodies_.LiveGeneration(slot));
        if (body != nullptr)
        {
            body->disturbed = false;
        }
    }
    // A joint that carried more than it can is removed.
    for (u32 slot = 0; slot < joints_.Slots(); ++slot)
    {
        const u32 generation = joints_.LiveGeneration(slot);
        const JointRecord* joint = joints_.Find(slot, generation);
        if (joint != nullptr && joint->broken)
        {
            JointBreakEvent event;
            event.joint = JointId{slot, generation};
            event.body_a = joint->desc.body_a;
            event.body_b = joint->desc.body_b;
            const Body* ea = joint->desc.body_a.IsValid() ? Find(joint->desc.body_a) : nullptr;
            const Body* eb = joint->desc.body_b.IsValid() ? Find(joint->desc.body_b) : nullptr;
            event.entity_a = ea != nullptr ? ea->entity : ecs::Entity();
            event.entity_b = eb != nullptr ? eb->entity : ecs::Entity();
            event.force = joint->reaction_force;
            event.torque = joint->reaction_torque;
            events_.joint_breaks.Push(event);
            joints_.Release(slot, generation);
            ++step_stats_.joints_broken;
        }
    }
    // The queries' index follows the step's motion (the moving colliders, then the static tree if it waits).
    if (query_index_.Get() != nullptr)
    {
        query_index_->RefreshMoving(*this);
    }
    step_stats_.after_ns += os::MonotonicTimeNanos() - after_start;
    ++step_stats_.steps;
}

bool PhysicsWorld::Interpolated(BodyId id, f32 alpha, Vec3d* position, Quaternion* orientation) const
{
    const Body* body = Find(id);
    if (body == nullptr)
    {
        return false;
    }
    if (!step_config_.interpolate || body->settings.type == BodyType::Static)
    {
        *position = body->position;
        *orientation = body->orientation;
        return true;
    }
    const f32 t = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    // previous + (current - previous) * t: exact when the two are equal (the form `a * (1 - t) + b * t` in single
    // precision weights lost up to 2^-25 of the position, 3 mm at 100 km).
    *position = body->previous_position + (body->position - body->previous_position) * static_cast<f64>(t);
    const Quaternion& a = body->previous_orientation;
    const Quaternion& b = body->orientation;
    const f32 dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const f32 sign = dot < 0.0f ? -1.0f : 1.0f;  // the short way round
    const f32 u = 1.0f - t;
    *orientation = Quaternion(a.x * u + b.x * t * sign, a.y * u + b.y * t * sign, a.z * u + b.z * t * sign,
                              a.w * u + b.w * t * sign)
                       .Normalized();
    return true;
}

u64 PhysicsWorld::StateHash(bool* valid) const
{
    StateHasher hasher;
    const u32 slots = bodies_.Slots();
    for (u32 slot = 0; slot < slots; ++slot)
    {
        const Body* body = bodies_.Find(slot, bodies_.LiveGeneration(slot));
        if (body == nullptr)
        {
            continue;
        }
        hasher.AddF64(body->position.x);
        hasher.AddF64(body->position.y);
        hasher.AddF64(body->position.z);
        hasher.AddF32(body->orientation.x);
        hasher.AddF32(body->orientation.y);
        hasher.AddF32(body->orientation.z);
        hasher.AddF32(body->orientation.w);
        hasher.AddF32(body->linear_velocity.x);
        hasher.AddF32(body->linear_velocity.y);
        hasher.AddF32(body->linear_velocity.z);
        hasher.AddF32(body->angular_velocity.x);
        hasher.AddF32(body->angular_velocity.y);
        hasher.AddF32(body->angular_velocity.z);
        hasher.AddU8(body->sleeping ? 1 : 0);
    }
    if (valid != nullptr)
    {
        *valid = hasher.Valid();
    }
    return hasher.Value();
}

}  // namespace vse::physics
