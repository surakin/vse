#include "physics/debug_draw.h"

#include "lang/scalar.h"
#include "math/math_util.h"

namespace vse::physics
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
        case ShapeKind::HeightField:
        case ShapeKind::Compound: Box(lines, center, r, shape.half_extents, color); break;
        }
    }
}

// The triangle edges of the cells of every height field that lie under another collider's box.
void FieldCells(const PhysicsWorld& world, Vector<spatial::DebugLine>* lines)
{
    constexpr u32 kMaxTriangles = 64;
    const u32 slots = world.Stats().collider_slots;
    for (u32 fs = 0; fs < slots; ++fs)
    {
        const ColliderRecord* field_record = world.FindCollider(world.ColliderAtSlot(fs));
        if (field_record == nullptr || field_record->value.shape.kind != ShapeKind::HeightField)
        {
            continue;
        }
        const Body* field_body = world.Find(field_record->owner);
        const HeightField* field = world.HeightFields().Find(field_record->value.shape.height_field);
        if (field_body == nullptr || field == nullptr)
        {
            continue;
        }
        const Vec3 field_center = field_body->position + field_body->orientation.ToMat3() * field_record->value.offset;
        const Mat3 fr = (field_body->orientation * field_record->value.rotation).ToMat3();
        for (u32 os = 0; os < slots; ++os)
        {
            const ColliderRecord* other = world.FindCollider(world.ColliderAtSlot(os));
            const Body* other_body = other != nullptr ? world.Find(other->owner) : nullptr;
            if (other == nullptr || other_body == nullptr || other->value.shape.kind == ShapeKind::HeightField ||
                other_body->settings.type == BodyType::Static)
            {
                continue;
            }
            const Vec3 oc = other_body->position + other_body->orientation.ToMat3() * other->value.offset;
            const Aabb box = other->value.shape.Bounds(oc, other_body->orientation * other->value.rotation);
            const Vec3 half = (box.max - box.min) * 0.5f +
                              Vec3(field->CellSize(), field->CellSize(), field->CellSize());
            const Vec3 d = (box.min + box.max) * 0.5f - field_center;
            const Vec3 local_center(fr.col[0].Dot(d), fr.col[1].Dot(d), fr.col[2].Dot(d));
            const Vec3 local_half(Abs(fr.col[0].x) * half.x + Abs(fr.col[0].y) * half.y + Abs(fr.col[0].z) * half.z,
                                  Abs(fr.col[1].x) * half.x + Abs(fr.col[1].y) * half.y + Abs(fr.col[1].z) * half.z,
                                  Abs(fr.col[2].x) * half.x + Abs(fr.col[2].y) * half.y + Abs(fr.col[2].z) * half.z);
            Vector<u32> triangles;
            field->QueryBox(Aabb::FromMinMax(local_center - local_half, local_center + local_half), &triangles,
                            kMaxTriangles);
            for (usize i = 0; i < triangles.Size(); ++i)
            {
                Vec3 a;
                Vec3 b;
                Vec3 c;
                field->Triangle(triangles[i], &a, &b, &c);
                const Vec3 wa = field_center + fr * a;
                const Vec3 wb = field_center + fr * b;
                const Vec3 wc = field_center + fr * c;
                Line(lines, wa, wb, kDebugStaticColor);  // the diagonal of an even triangle, the x edge of an odd one
                if ((triangles[i] & 1u) == 0)
                {
                    Line(lines, wb, wc, kDebugStaticColor);
                    Line(lines, wc, wa, kDebugStaticColor);
                }
                else
                {
                    Line(lines, wb, wc, kDebugStaticColor);
                }
            }
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
    if (options.field_cells)
    {
        FieldCells(world, lines);
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

}  // namespace vse::physics
