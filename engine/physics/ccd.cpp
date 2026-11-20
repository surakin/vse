// Continuous collision (Phase 25 Day 402): a body that moved fast enough to pass through thin static geometry is moved
// back to the time its shape first touches it. The time is found by sampling the sweep and bisecting the first
// overlapping interval, with the same narrow phase as the step (so every shape pair is covered); only + - * / and Sqrt.
#include "algorithm.h"
#include "lang/scalar.h"
#include "physics/broad_phase.h"
#include "physics/collide.h"
#include "physics/world.h"

namespace vse::physics
{

namespace
{

// The most samples one sweep takes before bisecting.
constexpr u32 kMaxSamples = 256;
// Bisection steps once a sample overlaps.
constexpr u32 kBisections = 24;

Vec3 LerpPoint(const Vec3& a, const Vec3& b, f32 t)
{
    return a * (1.0f - t) + b * t;
}

// The normalized linear blend of two orientations, the short way round.
Quaternion BlendOrientation(const Quaternion& a, const Quaternion& b, f32 t)
{
    const f32 dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const f32 sign = dot < 0.0f ? -1.0f : 1.0f;
    const f32 u = 1.0f - t;
    const Quaternion q(a.x * u + b.x * t * sign, a.y * u + b.y * t * sign, a.z * u + b.z * t * sign,
                       a.w * u + b.w * t * sign);
    const f32 n2 = q.LengthSquared();
    return n2 > 1e-20f ? q.Normalized() : b;
}

}  // namespace

void PhysicsWorld::SweepContinuous()
{
    if (broad_.Get() == nullptr)
    {
        return;
    }
    const u32 slots = bodies_.Slots();
    Vector<f32> first_hit;  // the earliest time of impact of each body, above 1 for none
    first_hit.Resize(slots);
    for (u32 i = 0; i < slots; ++i)
    {
        first_hit[i] = 2.0f;
    }
    const Vector<ColliderPair>& pairs = broad_->Pairs();
    for (usize i = 0; i < pairs.Size(); ++i)
    {
        if (pairs[i].trigger)
        {
            continue;
        }
        const ColliderRecord* records[2] = {FindCollider(pairs[i].a), FindCollider(pairs[i].b)};
        if (records[0] == nullptr || records[1] == nullptr)
        {
            continue;
        }
        for (u32 side = 0; side < 2; ++side)
        {
            const ColliderRecord* mine = records[side];
            const ColliderRecord* other = records[1 - side];
            Body* mover = Find(mine->owner);
            const Body* target = Find(other->owner);
            if (mover == nullptr || target == nullptr || target->settings.type == BodyType::Dynamic ||
                mover->settings.type != BodyType::Dynamic || mover->sleeping)
            {
                continue;
            }
            const Vec3 p0 = mover->previous_position;
            const Vec3 p1 = mover->position;
            const Quaternion q0 = mover->previous_orientation;
            const Quaternion q1 = mover->orientation;
            const Vec3 travel = p1 - p0;
            const f32 length = Sqrt(travel.LengthSquared());
            const Vec3 half = mine->value.shape.LocalHalfExtents();
            const f32 smallest = 2.0f * Min(half.x, Min(half.y, half.z));
            // The step's own travel decides, not the velocity the body ended with.
            const bool flagged = (mover->settings.flags & kBodyContinuous) != 0;
            const bool fast = step_config_.ccd_threshold > 0.0f && length > step_config_.ccd_threshold * smallest;
            if ((!flagged && !fast) || length < 1e-6f)
            {
                continue;
            }
            ++step_stats_.ccd_queries;
            Pose pose_target;
            pose_target.position = target->position + target->orientation.ToMat3() * other->value.offset;
            pose_target.rotation = target->orientation * other->value.rotation;
            const auto overlaps = [&](f32 t, f32 margin)
            {
                const Quaternion q = BlendOrientation(q0, q1, t);
                Pose pose;
                pose.position = LerpPoint(p0, p1, t) + q.ToMat3() * mine->value.offset;
                pose.rotation = q * mine->value.rotation;
                ContactManifold manifold;
                return Collide(mine->value.shape, pose, other->value.shape, pose_target, margin, &manifold, &hulls_,
                               &meshes_, &compounds_) == CollideResult::Contact;
            };
            if (overlaps(0.0f, step_config_.contact_margin))
            {
                continue;  // already touching, or within the speculative margin: the contact solver has it
            }
            const f32 spacing = Max(0.25f * smallest, 1e-3f);
            u32 samples = static_cast<u32>(length / spacing) + 1;
            if (samples > kMaxSamples)
            {
                samples = kMaxSamples;
            }
            for (u32 k = 1; k <= samples; ++k)
            {
                const f32 t = static_cast<f32>(k) / static_cast<f32>(samples);
                if (!overlaps(t, 0.0f))
                {
                    continue;
                }
                f32 lo = static_cast<f32>(k - 1) / static_cast<f32>(samples);
                f32 hi = t;
                for (u32 b = 0; b < kBisections; ++b)
                {
                    const f32 mid = 0.5f * (lo + hi);
                    if (overlaps(mid, 0.0f))
                    {
                        hi = mid;
                    }
                    else
                    {
                        lo = mid;
                    }
                }
                if (lo < first_hit[mover->id.index])
                {
                    first_hit[mover->id.index] = lo;
                }
                break;
            }
        }
    }
    for (u32 slot = 0; slot < slots; ++slot)
    {
        Body* body = bodies_.Find(slot, bodies_.LiveGeneration(slot));
        if (body == nullptr || first_hit[slot] > 1.0f)
        {
            continue;
        }
        const f32 t = first_hit[slot];
        body->position = LerpPoint(body->previous_position, body->position, t);
        body->orientation = BlendOrientation(body->previous_orientation, body->orientation, t);
        RefreshInertia(body);
        ++step_stats_.ccd_hits;
    }
}

}  // namespace vse::physics
