#include "physics/character.h"

#include "lang/scalar.h"
#include "physics/query.h"

namespace vse::physics
{

namespace
{

// How far past a ledge's edge the step-up probe walks, in radii.
constexpr f32 kStepProbe = 1.0f;
// The smallest `y` of the normal of the surface a step lands on (the round bottom may rest on the step's edge).
constexpr f32 kStepLandingY = 0.2f;

// The moves a character makes within one step: the capsule is swept and slid; nothing here uses a transcendental.
struct Mover
{
    const PhysicsWorld* world;
    Shape capsule;
    BodyId self;
    f32 skin;
    f32 min_ground_y;

    PhysicsQueryFilter Filter(bool dynamic) const
    {
        PhysicsQueryFilter filter;
        filter.include_dynamic = dynamic;
        filter.ignore = Span<const BodyId>(&self, 1);
        return filter;
    }

    // Sweeps the capsule from `from` by `delta`; returns true on a hit, `fraction` the part of the move made before it.
    bool Cast(const Vec3d& from, const Vec3& delta, bool dynamic, PhysicsHit* hit) const
    {
        const f32 length = Sqrt(delta.LengthSquared());
        if (!(length > 1e-7f))
        {
            return false;
        }
        const Vec3 direction = delta * (1.0f / length);
        return ShapeCast(*world, capsule, from, Quaternion::Identity(), direction, length + skin, Filter(dynamic), hit);
    }

    // Collide and slide: moves from `from` by `delta` along the walls it meets (at most four planes in turn, two at a
    // crease). Returns the end point; `walls` counts the steep planes met and `last_normal` the last one.
    Vec3d Slide(const Vec3d& from, const Vec3& delta, bool upright_walls, u32* walls, Vec3* last_normal,
                f32* moved) const
    {
        Vec3d position = from;
        Vec3 remaining = delta;
        Vec3 planes[2];
        u32 plane_count = 0;
        f32 total = 0.0f;
        for (u32 iteration = 0; iteration < 4; ++iteration)
        {
            const f32 length = Sqrt(remaining.LengthSquared());
            if (!(length > 1e-6f))
            {
                break;
            }
            PhysicsHit hit;
            if (!Cast(position, remaining, false, &hit))
            {
                position = position + Vec3d(remaining);
                total += length;
                break;
            }
            const f32 free = FMax(0.0f, hit.distance - skin);
            const Vec3 direction = remaining * (1.0f / length);
            position = position + Vec3d(direction * free);
            total += free;
            Vec3 plane = hit.normal;
            if (hit.normal.y < min_ground_y)
            {
                if (walls != nullptr)
                {
                    ++*walls;
                    *last_normal = hit.normal;
                }
                // A wall is upright for the walk: the round bottom of the capsule must not ride up a corner.
                const f32 flat2 = plane.x * plane.x + plane.z * plane.z;
                if (upright_walls && flat2 > 1e-8f)
                {
                    plane = Vec3(plane.x, 0.0f, plane.z) * (1.0f / Sqrt(flat2));
                }
            }
            // What is left of the move, along the plane (and, at a crease, along the line of the two planes).
            Vec3 rest = direction * (length - free);
            rest = rest - plane * rest.Dot(plane);
            if (plane_count == 1 && rest.Dot(planes[0]) < -1e-6f)
            {
                Vec3 crease = planes[0].Cross(plane);
                const f32 c2 = crease.LengthSquared();
                if (!(c2 > 1e-10f))
                {
                    break;
                }
                crease = crease * (1.0f / Sqrt(c2));
                rest = crease * (direction * (length - free)).Dot(crease);
            }
            if (plane_count < 2)
            {
                planes[plane_count++] = plane;
            }
            else
            {
                planes[0] = planes[1];
                planes[1] = plane;
            }
            remaining = rest;
        }
        if (moved != nullptr)
        {
            *moved = total;
        }
        return position;
    }
};

}  // namespace

BodyStatus CharacterController::Create(PhysicsWorld* world, const CharacterConfig& config, const Vec3d& position)
{
    if (!IsFinite(config.radius) || !IsFinite(config.height) || !IsFinite(config.step_height) ||
        !IsFinite(config.min_ground_y) || !IsFinite(config.skin_width) || !IsFinite(config.gravity_scale) ||
        !(config.radius > 0.0f) || !(config.height >= 2.0f * config.radius) || config.step_height < 0.0f ||
        !(config.skin_width >= 1e-4f) || config.min_ground_y < 0.0f || config.min_ground_y > 1.0f)
    {
        return BodyStatus::Invalid;
    }
    Destroy(world);
    BodyDesc desc;
    desc.settings.type = BodyType::Kinematic;
    desc.position = position;
    BodyStatus status = BodyStatus::Ok;
    const BodyId id = world->CreateBody(desc, &status);
    if (!id.IsValid())
    {
        return status;
    }
    Collider collider;
    collider.shape = Shape::MakeCapsule(0.5f * config.height - config.radius, config.radius);
    status = world->AddCollider(id, collider);
    if (status != BodyStatus::Ok)
    {
        world->DestroyBody(id);
        return status;
    }
    body_ = id;
    config_ = config;
    state_ = CharacterState();
    return BodyStatus::Ok;
}

BodyStatus CharacterController::SetHeight(PhysicsWorld* world, f32 height)
{
    const Body* body = body_.IsValid() ? world->Find(body_) : nullptr;
    if (body == nullptr)
    {
        return BodyStatus::StaleId;
    }
    if (!IsFinite(height) || !(height >= 2.0f * config_.radius))
    {
        return BodyStatus::Invalid;
    }
    const CharacterState kept = state_;
    CharacterConfig config = config_;
    config.height = height;
    const Vec3d feet_kept = body->position + Vec3d(0.0, 0.5 * static_cast<f64>(height - config_.height), 0.0);
    if (height > config_.height)
    {
        // Standing up: the taller capsule must not start inside a ceiling or a wall.
        PhysicsQueryFilter filter;
        filter.include_dynamic = false;
        filter.ignore = Span<const BodyId>(&body_, 1);
        Vector<ColliderId> found;
        const Shape taller = Shape::MakeCapsule(0.5f * height - config.radius - config.skin_width, config.radius);
        Overlap(*world, taller, feet_kept, Quaternion::Identity(), filter, &found);
        if (found.Size() != 0)
        {
            return BodyStatus::Invalid;  // blocked
        }
    }
    const BodyStatus status = Create(world, config, feet_kept);
    if (status == BodyStatus::Ok)
    {
        state_ = kept;
    }
    return status;
}

void CharacterController::Destroy(PhysicsWorld* world)
{
    if (body_.IsValid())
    {
        world->DestroyBody(body_);
    }
    body_ = BodyId();
    state_ = CharacterState();
}

void CharacterController::Stop(PhysicsWorld* world)
{
    state_.vertical_speed = 0.0f;
    pending_ = PendingMove();
    if (body_.IsValid())
    {
        world->SetVelocity(body_, Velocity());
    }
}

void CharacterController::Jump(f32 speed)
{
    if (IsFinite(speed))
    {
        state_.vertical_speed = speed;
    }
}

CharacterEvents CharacterController::Move(PhysicsWorld* world, const Vec3& velocity, f32 dt)
{
    CharacterEvents events;
    Body* body = world->Find(body_);
    if (body == nullptr || !IsFinite(dt) || !(dt > 0.0f) || !IsFinite(velocity.x) || !IsFinite(velocity.z))
    {
        return events;
    }
    Mover mover;
    mover.world = world;
    mover.capsule = Shape::MakeCapsule(0.5f * config_.height - config_.radius, config_.radius);
    mover.self = body_;
    mover.skin = config_.skin_width;
    mover.min_ground_y = config_.min_ground_y;
    // A second move before the world steps goes on from where the first one took the character, so the two add up
    // instead of the second overwriting the first.
    const u64 steps = world->GetStepStats().steps;
    const bool continues = pending_.body == body_ && pending_.steps == steps && body->position.x == pending_.from.x &&
                           body->position.y == pending_.from.y && body->position.z == pending_.from.z;
    const Vec3d start = continues ? pending_.to : body->position;
    const bool was_grounded = state_.on_ground;
    // The velocity of what the character stands on, at the character.
    Vec3 platform(0.0f, 0.0f, 0.0f);
    if (was_grounded)
    {
        const Body* ground = world->Find(state_.ground_body);
        if (ground != nullptr && ground->settings.type != BodyType::Static)
        {
            platform = ground->linear_velocity + ground->angular_velocity.Cross((start - ground->position).Narrow());
        }
    }
    // Gravity while in the air.
    if (!was_grounded || state_.vertical_speed > 0.0f)
    {
        state_.vertical_speed += world->GetStepConfig().gravity.y * config_.gravity_scale * dt;
    }
    else
    {
        state_.vertical_speed = 0.0f;
    }
    const Vec3 horizontal = Vec3(velocity.x + platform.x, 0.0f, velocity.z + platform.z) * dt;
    // 1. The horizontal move.
    u32 walls = 0;
    f32 moved = 0.0f;
    Vec3 wall_normal(0.0f, 0.0f, 0.0f);
    Vec3 scratch_normal(0.0f, 0.0f, 0.0f);  // the walls of the step-up and of the vertical move are not reported
    Vec3d position = mover.Slide(start, horizontal, true, &walls, &wall_normal, &moved);
    const f32 wanted = Sqrt(horizontal.LengthSquared());
    // 2. A ledge in the way: up, forward, down.
    bool stepped = false;
    PhysicsHit step_ground;
    if (was_grounded && walls > 0 && moved < wanted - 1e-4f && config_.step_height > 0.0f)
    {
        PhysicsHit up;
        const Vec3 lift(0.0f, config_.step_height, 0.0f);
        const bool blocked_up = mover.Cast(start, lift, false, &up);
        const f32 up_free = blocked_up ? FMax(0.0f, up.distance - config_.skin_width) : config_.step_height;
        if (up_free > 1e-4f)
        {
            const Vec3d raised = start + Vec3d(0.0, static_cast<f64>(up_free), 0.0);
            f32 forward_moved = 0.0f;
            const Vec3d across = mover.Slide(raised, horizontal, true, nullptr, &scratch_normal, &forward_moved);
            // Is there a step to climb at all? Walk the raised capsule on by a radius more: it must get past the edge
            // and find flat ground no higher than the step height (a ramp's edge or a wall fails this). A tread
            // shorter than the radius fails it too (KNOWN-ISSUES.md).
            bool climbable = false;
            if (forward_moved > moved + 1e-3f && wanted > 1e-6f)
            {
                const Vec3 probe_delta = horizontal * ((wanted + kStepProbe * config_.radius) / wanted);
                f32 probe_moved = 0.0f;
                const Vec3d beyond = mover.Slide(raised, probe_delta, true, nullptr, &scratch_normal, &probe_moved);
                PhysicsHit top;
                climbable = probe_moved >= wanted + kStepProbe * config_.radius - 1e-3f &&
                            mover.Cast(beyond, Vec3(0.0f, -(up_free + config_.skin_width), 0.0f), false, &top) &&
                            top.normal.y >= config_.min_ground_y && top.distance - config_.skin_width < up_free - 1e-3f;
            }
            if (climbable)
            {
                PhysicsHit down;
                if (mover.Cast(across, Vec3(0.0f, -(up_free + config_.skin_width), 0.0f), false, &down) &&
                    down.normal.y > kStepLandingY)
                {
                    const Vec3d landed = across +
                                         Vec3d(0.0, -static_cast<f64>(FMax(0.0f, down.distance - config_.skin_width)),
                                               0.0);
                    if (landed.y > position.y + 1e-3f)
                    {
                        position = landed;
                        walls = 0;
                        stepped = true;  // standing on an edge is standing, whatever the contact normal says
                        step_ground = down;
                    }
                }
            }
        }
    }
    // 3. The vertical move (gravity, a jump, a lift), sliding down what is too steep to stand on.
    const f32 vertical = (state_.vertical_speed + platform.y) * dt;
    u32 ignored_walls = 0;
    const f64 y_before = position.y;
    position = mover.Slide(position, Vec3(0.0f, vertical, 0.0f), false, &ignored_walls, &scratch_normal, nullptr);
    if (state_.vertical_speed > 0.0f && position.y - y_before < 0.5 * static_cast<f64>(vertical))
    {
        state_.vertical_speed = 0.0f;  // a ceiling stopped the rise: the jump ends there instead of hanging on it
    }
    // 4. Walking down a slope or off a small drop keeps the feet on the ground.
    const bool rising = state_.vertical_speed > 0.0f;
    if (was_grounded && !rising)
    {
        PhysicsHit snap;
        if (mover.Cast(position, Vec3(0.0f, -config_.step_height, 0.0f), false, &snap) &&
            snap.normal.y >= config_.min_ground_y)
        {
            position.y -= static_cast<f64>(FMax(0.0f, snap.distance - config_.skin_width));
        }
    }
    // 5. Where the character stands now (dynamic bodies count as ground).
    PhysicsHit ground;
    bool standing = !rising && mover.Cast(position, Vec3(0.0f, -2.0f * config_.skin_width, 0.0f), true, &ground) &&
                    ground.normal.y >= config_.min_ground_y;
    if (stepped && !rising)
    {
        standing = true;
        ground = step_ground;
    }
    events.began_ground = standing && !was_grounded;
    events.ended_ground = !standing && was_grounded;
    state_.on_ground = standing;
    state_.ground_body = standing ? ground.body : BodyId();
    state_.ground_normal = standing ? ground.normal : Vec3(0.0f, 1.0f, 0.0f);
    if (standing && state_.vertical_speed < 0.0f)
    {
        state_.vertical_speed = 0.0f;
    }
    events.wall_hits = walls;
    events.wall_normal = wall_normal;
    // The body carries the character there in the step, pushing what is in the way.
    Velocity v;
    v.linear = (position - body->position).Narrow() * (1.0f / dt);
    pending_.body = body_;
    pending_.steps = steps;
    pending_.from = body->position;
    pending_.to = position;
    world->SetVelocity(body_, v);
    return events;
}

}  // namespace vse::physics
