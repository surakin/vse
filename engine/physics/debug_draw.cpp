#include "physics/debug_draw.h"

#include "lang/scalar.h"
#include "math/math_util.h"

namespace engine_placeholder::physics
{

namespace
{

constexpr u32 kSegments = 16;

void Line(Vector<spatial::DebugLine>* lines, const Vec3& a, const Vec3& b, u32 color)
{
    spatial::DebugLine line;
    line.a = a;
    line.b = b;
    line.color = color;
    lines->Push(line);
}

// An arc of `count` segments in the plane of `u` and `v` around `center`, from angle `from` to `to`.
void Arc(Vector<spatial::DebugLine>* lines, const Vec3& center, const Vec3& u, const Vec3& v, f32 radius, f32 from,
         f32 to, u32 count, u32 color)
{
    Vec3 previous = center + u * (radius * Cos(from)) + v * (radius * Sin(from));
    for (u32 i = 1; i <= count; ++i)
    {
        const f32 angle = from + (to - from) * (static_cast<f32>(i) / static_cast<f32>(count));
        const Vec3 next = center + u * (radius * Cos(angle)) + v * (radius * Sin(angle));
        Line(lines, previous, next, color);
        previous = next;
    }
}

void Box(Vector<spatial::DebugLine>* lines, const Vec3& center, const Mat3& r, const Vec3& half, u32 color)
{
    Vec3 corner[8];
    for (u32 i = 0; i < 8; ++i)
    {
        const Vec3 local((i & 1u) != 0 ? half.x : -half.x, (i & 2u) != 0 ? half.y : -half.y,
                         (i & 4u) != 0 ? half.z : -half.z);
        corner[i] = center + r * local;
    }
    for (u32 i = 0; i < 8; ++i)
    {
        for (u32 bit = 1; bit < 8; bit <<= 1)
        {
            if ((i & bit) == 0)
            {
                Line(lines, corner[i], corner[i | bit], color);
            }
        }
    }
}

u32 IslandColor(u32 island)
{
    // A few distinct hues, picked by the island number.
    static constexpr u32 kPalette[8] = {0xFF40C040u, 0xFF4080FFu, 0xFFFF8040u, 0xFF40E0E0u,
                                        0xFFE040E0u, 0xFF80FF80u, 0xFFFFC0A0u, 0xFFA0A0FFu};
    return kPalette[island & 7u];
}

void Shapes(const PhysicsWorld& world, const DebugDrawOptions& options, Vector<spatial::DebugLine>* lines)
{
    const u32 slots = world.Stats().collider_slots;
    for (u32 slot = 0; slot < slots; ++slot)
    {
        const ColliderRecord* record = world.FindCollider(world.ColliderAtSlot(slot));
        if (record == nullptr)
        {
            continue;
        }
        const Body* body = world.Find(record->owner);
        if (body == nullptr)
        {
            continue;
        }
        const Mat3 rb = body->orientation.ToMat3();
        const Vec3 center = body->position + rb * record->value.offset;
        const Quaternion rotation = body->orientation * record->value.rotation;
        const Mat3 r = rotation.ToMat3();
        u32 color = kDebugStaticColor;
        if (body->settings.type == BodyType::Kinematic)
        {
            color = kDebugKinematicColor;
        }
        else if (body->settings.type == BodyType::Dynamic)
        {
            color = body->sleeping ? kDebugSleepingColor : (options.islands ? IslandColor(body->island) : 0xFF40C040u);
        }
        const Shape& shape = record->value.shape;
        switch (shape.kind)
        {
        case ShapeKind::Sphere:
            Arc(lines, center, r.col[0], r.col[1], shape.radius, 0.0f, 2.0f * kPi, kSegments, color);
            Arc(lines, center, r.col[1], r.col[2], shape.radius, 0.0f, 2.0f * kPi, kSegments, color);
            Arc(lines, center, r.col[2], r.col[0], shape.radius, 0.0f, 2.0f * kPi, kSegments, color);
            break;
        case ShapeKind::Capsule:
        {
            const Vec3 axis = r.col[1];
            const Vec3 top = center + axis * shape.half_height;
            const Vec3 bottom = center - axis * shape.half_height;
            Arc(lines, top, r.col[0], r.col[2], shape.radius, 0.0f, 2.0f * kPi, kSegments, color);
            Arc(lines, bottom, r.col[0], r.col[2], shape.radius, 0.0f, 2.0f * kPi, kSegments, color);
            Line(lines, top + r.col[0] * shape.radius, bottom + r.col[0] * shape.radius, color);
            Line(lines, top - r.col[0] * shape.radius, bottom - r.col[0] * shape.radius, color);
            Line(lines, top + r.col[2] * shape.radius, bottom + r.col[2] * shape.radius, color);
            Line(lines, top - r.col[2] * shape.radius, bottom - r.col[2] * shape.radius, color);
            Arc(lines, top, r.col[0], r.col[1], shape.radius, 0.0f, kPi, kSegments / 2, color);
            Arc(lines, top, r.col[2], r.col[1], shape.radius, 0.0f, kPi, kSegments / 2, color);
            Arc(lines, bottom, r.col[0], r.col[1] * -1.0f, shape.radius, 0.0f, kPi, kSegments / 2, color);
            Arc(lines, bottom, r.col[2], r.col[1] * -1.0f, shape.radius, 0.0f, kPi, kSegments / 2, color);
            break;
        }
        case ShapeKind::Box:
        case ShapeKind::Hull:
        case ShapeKind::Mesh:
        case ShapeKind::Compound: Box(lines, center, r, shape.half_extents, color); break;
        }
    }
}

void Boxes(const PhysicsWorld& world, Vector<spatial::DebugLine>* lines)
{
    const u32 slots = world.Stats().collider_slots;
    for (u32 slot = 0; slot < slots; ++slot)
    {
        const ColliderRecord* record = world.FindCollider(world.ColliderAtSlot(slot));
        const Body* body = record != nullptr ? world.Find(record->owner) : nullptr;
        if (body == nullptr)
        {
            continue;
        }
        const Vec3 center = body->position + body->orientation.ToMat3() * record->value.offset;
        const Aabb box = record->value.shape.Bounds(center, body->orientation * record->value.rotation);
        const Vec3 half = (box.max - box.min) * 0.5f;
        Box(lines, (box.min + box.max) * 0.5f, Mat3::Identity(), half, kDebugBroadPhaseColor);
    }
}

void Contacts(const PhysicsWorld& world, const DebugDrawOptions& options, Vector<spatial::DebugLine>* lines)
{
    const Vector<ContactRecord>& contacts = world.Contacts();
    for (usize i = 0; i < contacts.Size(); ++i)
    {
        const ContactRecord& record = contacts[i];
        for (u32 k = 0; k < record.manifold.count; ++k)
        {
            const Vec3 point = record.manifold.points[k].position_a;
            Line(lines, point, point + record.manifold.normal * options.normal_length, kDebugContactColor);
        }
    }
}

Vec3 WorldAnchor(const PhysicsWorld& world, BodyId id, const Vec3& local)
{
    const Body* body = id.IsValid() ? world.Find(id) : nullptr;
    return body != nullptr ? body->position + body->orientation.ToMat3() * local : local;
}

void Joints(const PhysicsWorld& world, Vector<spatial::DebugLine>* lines)
{
    const u32 slots = world.Stats().joint_slots;
    for (u32 slot = 0; slot < slots; ++slot)
    {
        const JointRecord* joint = world.FindJoint(world.JointAtSlot(slot));
        if (joint == nullptr)
        {
            continue;
        }
        const Vec3 a = WorldAnchor(world, joint->desc.body_a, joint->desc.anchor_a);
        const Vec3 b = WorldAnchor(world, joint->desc.body_b, joint->desc.anchor_b);
        Line(lines, a, b, kDebugJointColor);
        if (joint->desc.type == JointType::Hinge)
        {
            const Body* body_a = joint->desc.body_a.IsValid() ? world.Find(joint->desc.body_a) : nullptr;
            const Quaternion qa = (body_a != nullptr ? body_a->orientation : Quaternion::Identity()) *
                                  joint->desc.frame_a;
            Line(lines, a, a + qa.RotateVector(Vec3(0.0f, 0.0f, 0.5f)), kDebugJointColor);
        }
    }
}

}  // namespace

u32 DebugDraw(const PhysicsWorld& world, const DebugDrawOptions& options, Vector<spatial::DebugLine>* lines)
{
    const usize before = lines->Size();
    if (options.shapes)
    {
        Shapes(world, options, lines);
    }
    if (options.boxes)
    {
        Boxes(world, lines);
    }
    if (options.contacts)
    {
        Contacts(world, options, lines);
    }
    if (options.joints)
    {
        Joints(world, lines);
    }
    return static_cast<u32>(lines->Size() - before);
}

}  // namespace engine_placeholder::physics
