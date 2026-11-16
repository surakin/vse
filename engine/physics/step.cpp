// The fixed-step driver and the integration (Phase 25 Day 378). Everything here uses only + - * / and Sqrt
// (docs/guide/physics.md, "The rules of the step").
#include "lang/scalar.h"
#include "log/assert.h"
#include "physics/broad_phase.h"
#include "physics/broad_phase.h"
#include "physics/check.h"
#include "physics/collide.h"
#include "physics/collide.h"
#include "physics/determinism.h"
#include "physics/state_hash.h"
#include "physics/world.h"

namespace engine_placeholder::physics
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
        config.joint_damping_ratio > 1000.0f)
    {
        return BodyStatus::Invalid;
    }
    if (config.step_hz != step_config_.step_hz)
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
        const Vec3 center = body->position + offset + body->linear_velocity * dt;
        body->orientation = Turn(body->orientation, body->angular_velocity, dt);
        body->position = center - body->orientation.ToMat3() * body->mass.center;
        RefreshInertia(body);
    }
    else if (body->settings.type == BodyType::Kinematic)
    {
        // A kinematic body is moved by its velocity, about its origin; nothing pushes it.
        body->position = body->position + body->linear_velocity * dt;
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
    step_stats_.last_substeps = ran;
    return ran;
}

void PhysicsWorld::Step()
{
    // The rules of the step hold only if the process keeps denormals; a host that set flush-to-zero gets a report and a
    // counter, not a silent difference.
    const bool denormals_kept = DenormalsPreserved();
    if (!ENGINE_ENSURE(denormals_kept,
                       "the physics step runs with flush-to-zero or denormals-are-zero set: results are not portable"))
    {
        ++step_stats_.denormal_mode_steps;
    }
    const f32 dt = static_cast<f32>(1.0 / static_cast<f64>(step_config_.step_hz));
    const u32 slots = bodies_.Slots();
    // The previous poses are kept for the interpolation.
    for (u32 slot = 0; slot < slots; ++slot)
    {
        Body* body = bodies_.Find(slot, bodies_.LiveGeneration(slot));
        if (body != nullptr && body->settings.type != BodyType::Static)
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
        broad_ = MakeUnique<BroadPhase>(broad_config);
    }
    broad_->Update(*this);
    // The body pairs of joints that keep their bodies from colliding, as sorted keys.
    Vector<u64> connected;
    for (u32 slot = 0; slot < joints_.Slots(); ++slot)
    {
        const JointRecord* joint = joints_.Find(slot, joints_.LiveGeneration(slot));
        if (joint != nullptr && !joint->desc.collide_connected && joint->desc.body_b.IsValid())
        {
            const u64 x = joint->desc.body_a.ToU64();
            const u64 y = joint->desc.body_b.ToU64();
            connected.Push(x < y ? x : y);
            connected.Push(x < y ? y : x);
        }
    }
    Vector<ContactInput> inputs;
    const Vector<ColliderPair>& pairs = broad_->Pairs();
    for (usize i = 0; i < pairs.Size(); ++i)
    {
        const ColliderPair& pair = pairs[i];
        if (pair.trigger)
        {
            continue;
        }
        const ColliderRecord* ra = FindCollider(pair.a);
        const ColliderRecord* rb = FindCollider(pair.b);
        if (ra == nullptr || rb == nullptr || ra->owner == rb->owner)
        {
            continue;
        }
        bool joined = false;
        for (usize k = 0; k < connected.Size(); k += 2)
        {
            const u64 x = ra->owner.ToU64();
            const u64 y = rb->owner.ToU64();
            joined = joined || (connected[k] == (x < y ? x : y) && connected[k + 1] == (x < y ? y : x));
        }
        if (joined)
        {
            continue;
        }
        const Body* ba = Find(ra->owner);
        const Body* bb = Find(rb->owner);
        if (ba == nullptr || bb == nullptr ||
            (ba->settings.type != BodyType::Dynamic && bb->settings.type != BodyType::Dynamic))
        {
            continue;
        }
        Pose pose_a;
        pose_a.position = ba->position + ba->orientation.ToMat3() * ra->value.offset;
        pose_a.rotation = ba->orientation * ra->value.rotation;
        Pose pose_b;
        pose_b.position = bb->position + bb->orientation.ToMat3() * rb->value.offset;
        pose_b.rotation = bb->orientation * rb->value.rotation;
        ContactInput input;
        if (Collide(ra->value.shape, pose_a, rb->value.shape, pose_b, step_config_.contact_margin, &input.manifold,
                    &hulls_, &meshes_, &compounds_) != CollideResult::Contact)
        {
            continue;
        }
        CombineMaterials(ra->value.material, rb->value.material, &input.manifold);
        input.a = pair.a;
        input.b = pair.b;
        input.body_a = ra->owner;
        input.body_b = rb->owner;
        inputs.Push(input);
    }
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
    solver_.Solve(this, Span<const ContactInput>(inputs.Data(), inputs.Size()), solver_config);
    for (u32 slot = 0; slot < slots; ++slot)
    {
        Body* body = bodies_.Find(slot, bodies_.LiveGeneration(slot));
        if (body == nullptr || body->settings.type == BodyType::Static)
        {
            continue;
        }
        if (!body->integrated)
        {
            IntegrateVelocity(step_config_.gravity, body, dt);
            LimitVelocity(step_config_.max_velocity, body);
            IntegratePosition(body, dt);
        }
        body->integrated = false;
        body->force = Vec3(0.0f, 0.0f, 0.0f);
        body->torque = Vec3(0.0f, 0.0f, 0.0f);
    }
    // A joint that carried more than it can is removed.
    for (u32 slot = 0; slot < joints_.Slots(); ++slot)
    {
        const u32 generation = joints_.LiveGeneration(slot);
        const JointRecord* joint = joints_.Find(slot, generation);
        if (joint != nullptr && joint->broken)
        {
            joints_.Release(slot, generation);
            ++step_stats_.joints_broken;
        }
    }
    ++step_stats_.steps;
}

bool PhysicsWorld::Interpolated(BodyId id, f32 alpha, Vec3* position, Quaternion* orientation) const
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
    *position = body->previous_position * (1.0f - t) + body->position * t;
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
        hasher.AddF32(body->position.x);
        hasher.AddF32(body->position.y);
        hasher.AddF32(body->position.z);
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

}  // namespace engine_placeholder::physics
