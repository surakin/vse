#include "bytes/byte_writer.h"
#include "containers/hash.h"
#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "lang/utility.h"
#include "physics/check.h"
#include "physics/query_index.h"
#include "physics/snapshot.h"
#include "physics/snapshot_io.h"
#include "physics/world.h"

namespace vse::physics
{

namespace
{

constexpr u32 kMagic = 0x4e534850u;  // "PHSN"
// 5: the content hash of each collider's height field follows the triggers; 6: of each hull, mesh and compound too.
constexpr u32 kVersion = 6;
constexpr usize kMaxSlots = 1u << 24;

// ---- pools -----------------------------------------------------------------------------------------------------

template <class T, class F> void WritePool(const SlotPool<T>& pool, SnapshotOut& out, F&& write_value)
{
    const u32 slots = pool.Slots();
    out.Count(slots);
    for (u32 i = 0; i < slots; ++i)
    {
        out.U32(pool.SlotGeneration(i));
        out.Bool(pool.LiveGeneration(i) != 0);
    }
    out.Count(pool.FreeSlots().Size());
    for (usize i = 0; i < pool.FreeSlots().Size(); ++i)
    {
        out.U32(pool.FreeSlots()[i]);
    }
    for (u32 i = 0; i < slots; ++i)
    {
        if (pool.LiveGeneration(i) != 0)
        {
            write_value(*pool.Find(i, pool.SlotGeneration(i)));
        }
    }
}

// Reads a pool: the slots first, then the live values through `read_value(in, value, index, generation)`.
template <class T, class F> bool ReadPool(SlotPool<T>* pool, SnapshotIn& in, F&& read_value)
{
    // A slot takes at least 5 bytes, so a count above the bytes left is refused before anything is allocated.
    const usize slots = in.Count(kMaxSlots < in.Remaining() / 5 ? kMaxSlots : in.Remaining() / 5);
    Vector<u32> generations;
    Vector<u8> alive;
    for (usize i = 0; i < slots && in.Ok(); ++i)
    {
        generations.Push(in.U32());
        alive.Push(in.Bool() ? 1 : 0);
    }
    const usize free_count = in.Count(slots);
    Vector<u32> free;
    for (usize i = 0; i < free_count && in.Ok(); ++i)
    {
        free.Push(in.U32());
    }
    if (!in.Ok() ||
        !pool->Rebuild(Span<const u32>(generations.Data(), generations.Size()),
                       Span<const u8>(alive.Data(), alive.Size()), Span<const u32>(free.Data(), free.Size())))
    {
        in.Fail();
        return false;
    }
    for (usize i = 0; i < slots && in.Ok(); ++i)
    {
        if (alive[i] != 0)
        {
            read_value(in, pool->Find(static_cast<u32>(i), generations[i]), static_cast<u32>(i), generations[i]);
        }
    }
    return in.Ok();
}

// ---- values ----------------------------------------------------------------------------------------------------

void WriteConfig(SnapshotOut& out, const StepConfig& c)
{
    out.F32(c.step_hz);
    out.U32(c.max_substeps);
    out.V3(c.gravity);
    out.F32(c.contact_margin);
    out.Bool(c.interpolate);
    out.U32(c.velocity_iterations);
    out.F32(c.contact_slop);
    out.F32(c.restitution_threshold);
    out.U32(c.substeps);
    out.F32(c.contact_hertz);
    out.F32(c.contact_damping_ratio);
    out.F32(c.contact_push_velocity);
    out.F32(c.max_velocity);
    out.F32(c.joint_hertz);
    out.F32(c.joint_damping_ratio);
    out.F32(c.ccd_threshold);
    out.U32(c.threads);
    out.Bool(c.allow_sleep);
    out.F32(c.sleep_linear);
    out.F32(c.sleep_angular);
    out.F32(c.sleep_time);
    out.Bool(c.stay_events);
    out.Bool(c.debug_draw);
}

StepConfig ReadConfig(SnapshotIn& in)
{
    StepConfig c;
    c.step_hz = in.F32();
    c.max_substeps = in.U32();
    c.gravity = in.V3();
    c.contact_margin = in.F32();
    c.interpolate = in.Bool();
    c.velocity_iterations = in.U32();
    c.contact_slop = in.F32();
    c.restitution_threshold = in.F32();
    c.substeps = in.U32();
    c.contact_hertz = in.F32();
    c.contact_damping_ratio = in.F32();
    c.contact_push_velocity = in.F32();
    c.max_velocity = in.F32();
    c.joint_hertz = in.F32();
    c.joint_damping_ratio = in.F32();
    c.ccd_threshold = in.F32();
    c.threads = in.U32();
    c.allow_sleep = in.Bool();
    c.sleep_linear = in.F32();
    c.sleep_angular = in.F32();
    c.sleep_time = in.F32();
    c.stay_events = in.Bool();
    c.debug_draw = in.Bool();
    return c;
}

void WriteBody(SnapshotOut& out, const Body& b)
{
    out.Id(b.id.index, b.id.generation);
    out.Id(b.entity.index, b.entity.generation);
    out.U8(static_cast<u32>(b.settings.type));
    out.U32(b.settings.flags);
    out.F32(b.settings.mass);
    out.F32(b.settings.linear_damping);
    out.F32(b.settings.angular_damping);
    out.F32(b.settings.gravity_scale);
    out.V3d(b.position);
    out.Q(b.orientation);
    out.V3d(b.previous_position);
    out.Q(b.previous_orientation);
    out.V3(b.linear_velocity);
    out.V3(b.angular_velocity);
    out.V3(b.force);
    out.V3(b.torque);
    out.Id(b.first_collider.index, b.first_collider.generation);
    out.U32(b.collider_count);
    out.Bool(b.sleeping);
    out.F32(b.sleep_time);
    out.U32(b.island);
    out.Bool(b.disturbed);
    out.Bool(b.integrated);
    out.U32(b.teleports);
}

void ReadBody(SnapshotIn& in, Body* b)
{
    b->id.index = in.U32();
    b->id.generation = in.U32();
    b->entity.index = in.U32();
    b->entity.generation = in.U32();
    b->settings.type = static_cast<BodyType>(in.U8());
    b->settings.flags = in.U32();
    b->settings.mass = in.F32();
    b->settings.linear_damping = in.F32();
    b->settings.angular_damping = in.F32();
    b->settings.gravity_scale = in.F32();
    b->position = in.V3d();
    b->orientation = in.Q();
    b->previous_position = in.V3d();
    b->previous_orientation = in.Q();
    b->linear_velocity = in.V3();
    b->angular_velocity = in.V3();
    b->force = in.V3();
    b->torque = in.V3();
    b->first_collider.index = in.U32();
    b->first_collider.generation = in.U32();
    b->collider_count = in.U32();
    b->sleeping = in.Bool();
    b->sleep_time = in.F32();
    b->island = in.U32();
    b->disturbed = in.Bool();
    b->integrated = in.Bool();
    b->teleports = in.U32();
}

// Whether the pair (xa, xb) comes strictly before (ya, yb): the order of the contact and trigger lists.
bool SnapshotKeysBefore(u64 xa, u64 xb, u64 ya, u64 yb)
{
    return xa != ya ? xa < ya : xb < yb;
}

bool UnitQuaternion(const Quaternion& q)
{
    return IsUnitQuaternion(q);
}

bool BodyValid(const Body& b, u32 slot, u32 generation, u32 body_slots)
{
    if (b.id.index != slot || b.id.generation != generation || b.settings.Validate() != Validation::Ok ||
        !UnitQuaternion(b.orientation) || !UnitQuaternion(b.previous_orientation) || !InsideWorld(b.position) ||
        !InsideWorld(b.previous_position) || !SnapshotFinite(b.linear_velocity) ||
        !SnapshotFinite(b.angular_velocity) || !SnapshotFinite(b.force) || !SnapshotFinite(b.torque) ||
        !SnapshotFinite(b.sleep_time) || b.island > body_slots)
    {
        return false;
    }
    return true;
}

void WriteCollider(SnapshotOut& out, const ColliderRecord& r)
{
    WriteColliderValue(out, r.value);
    out.Id(r.owner.index, r.owner.generation);
    out.Id(r.next.index, r.next.generation);
}

void ReadCollider(SnapshotIn& in, ColliderRecord* r)
{
    ReadColliderValue(in, &r->value);
    r->owner.index = in.U32();
    r->owner.generation = in.U32();
    r->next.index = in.U32();
    r->next.generation = in.U32();
}

void WriteJoint(SnapshotOut& out, const JointRecord& j)
{
    const JointDesc& d = j.desc;
    out.U8(static_cast<u32>(d.type));
    out.Id(d.body_a.index, d.body_a.generation);
    out.Id(d.body_b.index, d.body_b.generation);
    out.V3(d.anchor_a);
    out.V3(d.anchor_b);
    out.V3d(d.world_origin);
    out.Q(d.frame_a);
    out.Q(d.frame_b);
    out.F32(d.length);
    out.F32(d.min_length);
    out.F32(d.max_length);
    out.F32(d.hertz);
    out.F32(d.damping_ratio);
    out.F32(d.break_force);
    out.F32(d.break_torque);
    out.Bool(d.collide_connected);
    out.Bool(d.enable_limit);
    out.F32(d.lower_angle);
    out.F32(d.upper_angle);
    out.Bool(d.enable_motor);
    out.F32(d.motor_speed);
    out.F32(d.max_motor_torque);
    out.V3(j.linear_impulse);
    out.V3(j.angular_impulse);
    out.F32(j.axial_impulse);
    out.F32(j.lower_impulse);
    out.F32(j.upper_impulse);
    out.F32(j.motor_impulse);
    out.F32(j.spring_impulse);
    out.F32(j.angle);
    out.F32(j.angle_raw);
    out.V3(j.reaction_force);
    out.V3(j.reaction_torque);
    out.Bool(j.broken);
}

void ReadJoint(SnapshotIn& in, JointRecord* j)
{
    JointDesc& d = j->desc;
    d.type = static_cast<JointType>(in.U8());
    d.body_a.index = in.U32();
    d.body_a.generation = in.U32();
    d.body_b.index = in.U32();
    d.body_b.generation = in.U32();
    d.anchor_a = in.V3();
    d.anchor_b = in.V3();
    d.world_origin = in.V3d();
    d.frame_a = in.Q();
    d.frame_b = in.Q();
    d.length = in.F32();
    d.min_length = in.F32();
    d.max_length = in.F32();
    d.hertz = in.F32();
    d.damping_ratio = in.F32();
    d.break_force = in.F32();
    d.break_torque = in.F32();
    d.collide_connected = in.Bool();
    d.enable_limit = in.Bool();
    d.lower_angle = in.F32();
    d.upper_angle = in.F32();
    d.enable_motor = in.Bool();
    d.motor_speed = in.F32();
    d.max_motor_torque = in.F32();
    j->linear_impulse = in.V3();
    j->angular_impulse = in.V3();
    j->axial_impulse = in.F32();
    j->lower_impulse = in.F32();
    j->upper_impulse = in.F32();
    j->motor_impulse = in.F32();
    j->spring_impulse = in.F32();
    j->angle = in.F32();
    j->angle_raw = in.F32();
    j->reaction_force = in.V3();
    j->reaction_torque = in.V3();
    j->broken = in.Bool();
}

bool JointValid(const JointRecord& j)
{
    const JointDesc& d = j.desc;
    return j.desc.Validate() == Validation::Ok && InsideWorld(d.anchor_a) && InsideWorld(d.anchor_b) &&
           SnapshotFinite(d.length) && SnapshotFinite(d.min_length) && SnapshotFinite(d.max_length) &&
           SnapshotFinite(d.hertz) && SnapshotFinite(d.damping_ratio) && SnapshotFinite(d.break_force) &&
           SnapshotFinite(d.break_torque) && SnapshotFinite(d.lower_angle) && SnapshotFinite(d.upper_angle) &&
           SnapshotFinite(d.motor_speed) && SnapshotFinite(d.max_motor_torque) && SnapshotFinite(j.linear_impulse) &&
           SnapshotFinite(j.angular_impulse) && SnapshotFinite(j.axial_impulse) && SnapshotFinite(j.lower_impulse) &&
           SnapshotFinite(j.upper_impulse) && SnapshotFinite(j.motor_impulse) && SnapshotFinite(j.spring_impulse) &&
           SnapshotFinite(j.angle) && SnapshotFinite(j.angle_raw) && SnapshotFinite(j.reaction_force) &&
           SnapshotFinite(j.reaction_torque) && UnitQuaternion(j.desc.frame_a) && UnitQuaternion(j.desc.frame_b);
}

void WriteContact(SnapshotOut& out, const ContactRecord& r)
{
    out.Id(r.a.index, r.a.generation);
    out.Id(r.b.index, r.b.generation);
    out.Id(r.body_a.index, r.body_a.generation);
    out.Id(r.body_b.index, r.body_b.generation);
    out.V3d(r.manifold.anchor);
    out.V3(r.manifold.normal);
    out.U32(r.manifold.count);
    for (u32 k = 0; k < r.manifold.count; ++k)
    {
        const ContactPoint& p = r.manifold.points[k];
        out.V3(p.position_a);
        out.V3(p.position_b);
        out.F32(p.depth);
        out.U32(p.feature_id);
    }
    out.F32(r.manifold.friction);
    out.F32(r.manifold.restitution);
    out.F32(r.normal_impulse);
}

void ReadContact(SnapshotIn& in, ContactRecord* r)
{
    r->a.index = in.U32();
    r->a.generation = in.U32();
    r->b.index = in.U32();
    r->b.generation = in.U32();
    r->body_a.index = in.U32();
    r->body_a.generation = in.U32();
    r->body_b.index = in.U32();
    r->body_b.generation = in.U32();
    r->manifold.anchor = in.V3d();
    if (!SnapshotFinite(r->manifold.anchor))
    {
        in.Fail();
    }
    r->manifold.normal = in.V3();
    r->manifold.count = in.U32();
    if (r->manifold.count > 4)
    {
        in.Fail();
        r->manifold.count = 0;
    }
    for (u32 k = 0; k < r->manifold.count; ++k)
    {
        ContactPoint& p = r->manifold.points[k];
        p.position_a = in.V3();
        p.position_b = in.V3();
        p.depth = in.F32();
        p.feature_id = in.U32();
        if (!SnapshotFinite(p.position_a) || !SnapshotFinite(p.position_b) || !SnapshotFinite(p.depth))
        {
            in.Fail();
        }
    }
    r->manifold.friction = in.F32();
    r->manifold.restitution = in.F32();
    r->normal_impulse = in.F32();
    if (!SnapshotFinite(r->manifold.normal) || !SnapshotFinite(r->manifold.friction) ||
        !SnapshotFinite(r->manifold.restitution) || !SnapshotFinite(r->normal_impulse))
    {
        in.Fail();
    }
}

void WriteTrigger(SnapshotOut& out, const TriggerEvent& t)
{
    out.Id(t.trigger.index, t.trigger.generation);
    out.Id(t.other.index, t.other.generation);
    out.Id(t.body_trigger.index, t.body_trigger.generation);
    out.Id(t.body_other.index, t.body_other.generation);
    out.Id(t.entity_trigger.index, t.entity_trigger.generation);
    out.Id(t.entity_other.index, t.entity_other.generation);
}

void ReadTrigger(SnapshotIn& in, TriggerEvent* t)
{
    t->enter = true;
    t->trigger.index = in.U32();
    t->trigger.generation = in.U32();
    t->other.index = in.U32();
    t->other.generation = in.U32();
    t->body_trigger.index = in.U32();
    t->body_trigger.generation = in.U32();
    t->body_other.index = in.U32();
    t->body_other.generation = in.U32();
    t->entity_trigger.index = in.U32();
    t->entity_trigger.generation = in.U32();
    t->entity_other.index = in.U32();
    t->entity_other.generation = in.U32();
}

u64 FloatBits(f32 v)
{
    u32 b = 0;
    MemCopy(&b, &v, sizeof(b));
    return static_cast<u64>(b);
}

void AddVec(Fnv1a64Builder* h, const Vec3& v)
{
    h->AddWord(FloatBits(v.x));
    h->AddWord(FloatBits(v.y));
    h->AddWord(FloatBits(v.z));
}

u64 HullHash(const ConvexHull& hull)
{
    Fnv1a64Builder h;
    h.AddWord(hull.VertexCount());
    h.AddWord(hull.FaceCount());
    for (u32 i = 0; i < hull.VertexCount(); ++i)
    {
        AddVec(&h, hull.Vertex(i));
    }
    for (u32 f = 0; f < hull.FaceCount(); ++f)
    {
        const HullFace& face = hull.Face(f);
        h.AddWord(face.first);
        h.AddWord(face.count);
        AddVec(&h, face.normal);
        h.AddWord(FloatBits(face.offset));
        for (u32 k = 0; k < face.count; ++k)
        {
            h.AddWord(hull.FaceVertex(face.first + k));
        }
    }
    return h.Value();
}

u64 MeshHash(const TriangleMesh& mesh)
{
    Fnv1a64Builder h;
    h.AddWord(mesh.VertexCount());
    h.AddWord(mesh.TriangleCount());
    for (u32 i = 0; i < mesh.VertexCount(); ++i)
    {
        AddVec(&h, mesh.Vertex(i));
    }
    for (u32 t = 0; t < mesh.TriangleCount(); ++t)
    {
        h.AddWord(mesh.Index(t, 0));
        h.AddWord(mesh.Index(t, 1));
        h.AddWord(mesh.Index(t, 2));
        h.AddWord(mesh.Material(t));
    }
    return h.Value();
}

}  // namespace

void WriteColliderValue(SnapshotOut& out, const Collider& c)
{
    out.U8(static_cast<u32>(c.shape.kind));
    out.F32(c.shape.radius);
    out.F32(c.shape.half_height);
    out.V3(c.shape.half_extents);
    out.U32(c.shape.hull);
    out.U32(c.shape.mesh);
    out.U32(c.shape.compound);
    out.U32(c.shape.height_field);
    out.V3(c.offset);
    out.Q(c.rotation);
    out.F32(c.material.friction);
    out.F32(c.material.restitution);
    out.F32(c.material.density);
    out.U8(static_cast<u32>(c.material.friction_rule));
    out.U8(static_cast<u32>(c.material.restitution_rule));
    out.U8(c.layer);
    out.Bool(c.is_trigger);
    out.U32(c.group);
    out.U32(c.mask);
}

void ReadColliderValue(SnapshotIn& in, Collider* c)
{
    c->shape.kind = static_cast<ShapeKind>(in.U8());
    c->shape.radius = in.F32();
    c->shape.half_height = in.F32();
    c->shape.half_extents = in.V3();
    c->shape.hull = in.U32();
    c->shape.mesh = in.U32();
    c->shape.compound = in.U32();
    c->shape.height_field = in.U32();
    c->offset = in.V3();
    c->rotation = in.Q();
    c->material.friction = in.F32();
    c->material.restitution = in.F32();
    c->material.density = in.F32();
    c->material.friction_rule = static_cast<CombineRule>(in.U8());
    c->material.restitution_rule = static_cast<CombineRule>(in.U8());
    c->layer = static_cast<u8>(in.U8());
    c->is_trigger = in.Bool();
    c->group = in.U32();
    c->mask = in.U32();
}

const char* SnapshotStatusName(SnapshotStatus status)
{
    switch (status)
    {
    case SnapshotStatus::Ok: return "ok";
    case SnapshotStatus::BadMagic: return "bad-magic";
    case SnapshotStatus::UnknownVersion: return "unknown-version";
    case SnapshotStatus::Truncated: return "truncated";
    case SnapshotStatus::Corrupt: return "corrupt";
    case SnapshotStatus::LibraryMismatch: return "library-mismatch";
    case SnapshotStatus::OutOfMemory: return "out-of-memory";
    }
    return "unknown";
}

// The content of the library entry a shape names (a hull, mesh, compound or height field), so a snapshot can refuse a
// world whose library holds something else under the same id; 0 for a shape that names no entry or a missing one.
u64 PhysicsWorld::LibraryContentHash(const Shape& shape) const
{
    switch (shape.kind)
    {
    case ShapeKind::Hull:
    {
        const ConvexHull* hull = hulls_.Find(shape.hull);
        return hull != nullptr ? HullHash(*hull) : 0;
    }
    case ShapeKind::Mesh:
    {
        const TriangleMesh* mesh = meshes_.Find(shape.mesh);
        return mesh != nullptr ? MeshHash(*mesh) : 0;
    }
    case ShapeKind::HeightField:
    {
        const HeightField* field = height_fields_.Find(shape.height_field);
        return field != nullptr ? field->ContentHash() : 0;
    }
    case ShapeKind::Compound:
    {
        const CompoundShape* compound = compounds_.Find(shape.compound);
        if (compound == nullptr)
        {
            return 0;
        }
        Fnv1a64Builder h;
        h.AddWord(compound->ChildCount());
        for (u32 i = 0; i < compound->ChildCount(); ++i)
        {
            const CompoundChild& child = compound->Child(i);
            h.AddWord(static_cast<u64>(child.shape.kind));
            h.AddWord(FloatBits(child.shape.radius));
            h.AddWord(FloatBits(child.shape.half_height));
            AddVec(&h, child.shape.half_extents);
            AddVec(&h, child.offset);
            h.AddWord(FloatBits(child.rotation.x));
            h.AddWord(FloatBits(child.rotation.y));
            h.AddWord(FloatBits(child.rotation.z));
            h.AddWord(FloatBits(child.rotation.w));
            h.AddWord(FloatBits(child.material.friction));
            h.AddWord(FloatBits(child.material.restitution));
            h.AddWord(FloatBits(child.material.density));
            h.AddWord(child.group);
            h.AddWord(child.mask);
            h.AddWord(child.shape.kind == ShapeKind::Hull ? LibraryContentHash(child.shape) : 0);
        }
        return h.Value();
    }
    default: return 0;
    }
}

static bool NamesLibrary(ShapeKind kind)
{
    return kind == ShapeKind::Hull || kind == ShapeKind::Mesh || kind == ShapeKind::HeightField ||
           kind == ShapeKind::Compound;
}

void PhysicsWorld::Snapshot(bytes::ByteWriter* writer) const
{
    SnapshotOut out(writer);
    out.U32(kMagic);
    out.U32(kVersion);
    WriteConfig(out, step_config_);
    out.F64(accumulator_);
    out.U64(step_stats_.steps);
    WritePool(bodies_, out, [&](const Body& b) { WriteBody(out, b); });
    WritePool(colliders_, out, [&](const ColliderRecord& c) { WriteCollider(out, c); });
    WritePool(joints_, out, [&](const JointRecord& j) { WriteJoint(out, j); });
    solver_.SaveCache(&out);
    out.Count(contacts_.Size());
    for (usize i = 0; i < contacts_.Size(); ++i)
    {
        WriteContact(out, contacts_[i]);
    }
    out.Count(triggers_touching_.Size());
    for (usize i = 0; i < triggers_touching_.Size(); ++i)
    {
        WriteTrigger(out, triggers_touching_[i]);
    }
    // The library entries the colliders name (hulls, meshes, compounds, height fields), by content: a world with
    // something else under the same id refuses the snapshot.
    u32 named = 0;
    for (u32 slot = 0; slot < colliders_.Slots(); ++slot)
    {
        const u32 generation = colliders_.LiveGeneration(slot);
        named += generation != 0 && NamesLibrary(colliders_.Find(slot, generation)->value.shape.kind) ? 1u : 0u;
    }
    out.Count(named);
    for (u32 slot = 0; slot < colliders_.Slots(); ++slot)
    {
        const u32 generation = colliders_.LiveGeneration(slot);
        if (generation == 0 || !NamesLibrary(colliders_.Find(slot, generation)->value.shape.kind))
        {
            continue;
        }
        out.U32(slot);
        out.U64(LibraryContentHash(colliders_.Find(slot, generation)->value.shape));
    }
}

SnapshotStatus PhysicsWorld::Restore(ByteSpan data)
{
    SnapshotIn in(data);
    const u32 magic = in.U32();
    if (!in.Ok())
    {
        return SnapshotStatus::Truncated;
    }
    if (magic != kMagic)
    {
        return SnapshotStatus::BadMagic;
    }
    const u32 version = in.U32();
    if (!in.Ok())
    {
        return SnapshotStatus::Truncated;
    }
    if (version != kVersion)
    {
        return SnapshotStatus::UnknownVersion;
    }
    // The state is built in a world of its own and swapped in when everything is checked.
    PhysicsWorld next;
    const StepConfig config = ReadConfig(in);
    const f64 accumulator = in.F64();
    const u64 steps = in.U64();
    if (!in.Ok())
    {
        return SnapshotStatus::Truncated;
    }
    if (next.SetStepConfig(config) != BodyStatus::Ok || !IsFinite(accumulator) || accumulator < 0.0 ||
        accumulator > 1.0e6)
    {
        return SnapshotStatus::Corrupt;
    }
    next.accumulator_ = accumulator;
    next.step_stats_.steps = steps;
    bool library_missing = false;
    usize named_colliders = 0;
    usize checked_entries = 0;
    if (!ReadPool(&next.bodies_, in, [&](SnapshotIn& r, Body* b, u32, u32) { ReadBody(r, b); }) ||
        !ReadPool(&next.colliders_, in, [&](SnapshotIn& r, ColliderRecord* c, u32, u32) { ReadCollider(r, c); }) ||
        !ReadPool(&next.joints_, in, [&](SnapshotIn& r, JointRecord* j, u32, u32) { ReadJoint(r, j); }))
    {
        return in.Remaining() == 0 ? SnapshotStatus::Truncated : SnapshotStatus::Corrupt;
    }
    if (!next.solver_.LoadCache(&in))
    {
        return SnapshotStatus::Corrupt;
    }
    const usize contacts = in.Count(in.Remaining() / 60);
    for (usize i = 0; i < contacts && in.Ok(); ++i)
    {
        ContactRecord record;
        ReadContact(in, &record);
        next.contacts_.Push(record);
    }
    const usize triggers = in.Count(in.Remaining() / 48);
    for (usize i = 0; i < triggers && in.Ok(); ++i)
    {
        TriggerEvent record;
        ReadTrigger(in, &record);
        next.triggers_touching_.Push(record);
    }
    Vector<u32> field_slots;
    Vector<u64> field_hashes;
    const usize field_count = in.Count(in.Remaining() / 12);
    for (usize i = 0; i < field_count && in.Ok(); ++i)
    {
        field_slots.Push(in.U32());
        field_hashes.Push(in.U64());
    }
    if (!in.Ok())
    {
        return in.Remaining() == 0 ? SnapshotStatus::Truncated : SnapshotStatus::Corrupt;
    }
    if (in.Remaining() != 0)
    {
        return SnapshotStatus::Corrupt;
    }
    // The touching pairs are searched by binary search (events.cpp): each list must be sorted by collider keys, with no
    // pair twice.
    for (usize i = 1; i < next.contacts_.Size(); ++i)
    {
        const ContactRecord& x = next.contacts_[i - 1];
        const ContactRecord& y = next.contacts_[i];
        if (!SnapshotKeysBefore(x.a.ToKey(), x.b.ToKey(), y.a.ToKey(), y.b.ToKey()))
        {
            return SnapshotStatus::Corrupt;
        }
    }
    for (usize i = 1; i < next.triggers_touching_.Size(); ++i)
    {
        const TriggerEvent& x = next.triggers_touching_[i - 1];
        const TriggerEvent& y = next.triggers_touching_[i];
        if (!SnapshotKeysBefore(x.trigger.ToKey(), x.other.ToKey(), y.trigger.ToKey(), y.other.ToKey()))
        {
            return SnapshotStatus::Corrupt;
        }
    }
    // The values are checked as a whole: each body, its chain of colliders, each collider's owner and shape, each
    // joint's bodies.
    const u32 body_slots = next.bodies_.Slots();
    u32 chained = 0;
    for (u32 slot = 0; slot < body_slots; ++slot)
    {
        const u32 generation = next.bodies_.LiveGeneration(slot);
        if (generation == 0)
        {
            continue;
        }
        const Body* body = next.bodies_.Find(slot, generation);
        if (!BodyValid(*body, slot, generation, body_slots))
        {
            return SnapshotStatus::Corrupt;
        }
        ColliderId at = body->first_collider;
        u32 walked = 0;
        while (at.IsValid())
        {
            const ColliderRecord* record = next.colliders_.Find(at.index, at.generation);
            if (record == nullptr || !(record->owner == body->id) || ++walked > body->collider_count)
            {
                return SnapshotStatus::Corrupt;
            }
            at = record->next;
        }
        if (walked != body->collider_count)
        {
            return SnapshotStatus::Corrupt;
        }
        chained += walked;
    }
    if (chained != next.colliders_.Live())
    {
        return SnapshotStatus::Corrupt;  // a collider that no body owns
    }
    for (u32 slot = 0; slot < next.colliders_.Slots(); ++slot)
    {
        const u32 generation = next.colliders_.LiveGeneration(slot);
        if (generation == 0)
        {
            continue;
        }
        const ColliderRecord* record = next.colliders_.Find(slot, generation);
        const Body* owner = next.bodies_.Find(record->owner.index, record->owner.generation);
        if (owner == nullptr || record->value.Validate() != Validation::Ok)
        {
            return SnapshotStatus::Corrupt;
        }
        if (!HullKnown(record->value.shape, owner->settings.type))
        {
            library_missing = true;
        }
        named_colliders += NamesLibrary(record->value.shape.kind) ? 1u : 0u;
    }
    for (u32 slot = 0; slot < next.joints_.Slots(); ++slot)
    {
        const u32 generation = next.joints_.LiveGeneration(slot);
        if (generation == 0)
        {
            continue;
        }
        const JointRecord* joint = next.joints_.Find(slot, generation);
        const bool a_ok = !joint->desc.body_a.IsValid() ||
                          next.bodies_.Find(joint->desc.body_a.index, joint->desc.body_a.generation) != nullptr;
        const bool b_ok = !joint->desc.body_b.IsValid() ||
                          next.bodies_.Find(joint->desc.body_b.index, joint->desc.body_b.generation) != nullptr;
        if (!JointValid(*joint) || !a_ok || !b_ok)
        {
            return SnapshotStatus::Corrupt;
        }
    }
    if (library_missing)
    {
        return SnapshotStatus::LibraryMismatch;
    }
    for (usize i = 0; i < field_slots.Size(); ++i)
    {
        const u32 generation = field_slots[i] < next.colliders_.Slots() ? next.colliders_.LiveGeneration(field_slots[i])
                                                                        : 0;
        const ColliderRecord* record = generation != 0 ? next.colliders_.Find(field_slots[i], generation) : nullptr;
        if (record == nullptr || !NamesLibrary(record->value.shape.kind) ||
            (i > 0 && field_slots[i] <= field_slots[i - 1]))
        {
            return SnapshotStatus::Corrupt;
        }
        if (LibraryContentHash(record->value.shape) != field_hashes[i])
        {
            return SnapshotStatus::LibraryMismatch;  // the same id names another hull, mesh, compound or map here
        }
        ++checked_entries;
    }
    if (checked_entries != named_colliders)
    {
        return SnapshotStatus::Corrupt;  // a collider that names an entry and has no hash
    }
    // The mass, the centre of mass and the inertia are not stored: they follow from the settings and the shape, so they
    // are made again here (with the libraries of this world), exactly as `RecomputeMass` made them the first time.
    next.hulls_ = Move(hulls_);
    next.meshes_ = Move(meshes_);
    next.height_fields_ = Move(height_fields_);
    next.compounds_ = Move(compounds_);
    bool masses_ok = true;
    for (u32 slot = 0; slot < body_slots; ++slot)
    {
        const u32 generation = next.bodies_.LiveGeneration(slot);
        if (generation == 0)
        {
            continue;
        }
        Body* body = next.bodies_.Find(slot, generation);
        next.RecomputeMass(body);
        masses_ok = masses_ok && IsFinite(body->mass.mass) && IsFinite(body->mass.volume) &&
                    IsFinite(body->mass.center.x) && IsFinite(body->mass.center.y) && IsFinite(body->mass.center.z) &&
                    IsFinite(body->mass.inertia.x) && IsFinite(body->mass.inertia.y) &&
                    IsFinite(body->mass.inertia.z) && IsFinite(body->inverse_mass) &&
                    (body->inverse_mass == 0.0f || (body->mass.mass >= kMinMass && body->mass.mass <= kMaxMass));
    }
    if (!masses_ok)
    {
        hulls_ = Move(next.hulls_);
        meshes_ = Move(next.meshes_);
        height_fields_ = Move(next.height_fields_);
        compounds_ = Move(next.compounds_);
        return SnapshotStatus::Corrupt;
    }
    next.scheduler_ = scheduler_;
    next.pre_step_ = pre_step_;  // the program's hook (the characters' moves) is not world state: it stays installed
    next.pre_step_user_ = pre_step_user_;
    *this = Move(next);
    RebuildJointIndex();  // derived data: the restored joints, by body
    // The restored colliders, in the queries' index (the restored world was built without it).
    if (colliders_.Live() != 0)
    {
        query_index_ = MakeUnique<QueryIndex>();
        query_index_->Rebuild(*this);
    }
    return SnapshotStatus::Ok;
}

storage::Status SaveSnapshot(const PhysicsWorld& world, storage::Store* store, storage::EntityKey key)
{
    if (key.IsNull())
    {
        return storage::Status::Make(storage::ErrorCode::Invalid, "a snapshot needs a non-null key");
    }
    bytes::ByteWriter writer;
    world.Snapshot(&writer);
    return store->Put(storage::EntityRecord::Make(key, writer.Bytes()));
}

storage::Status LoadSnapshot(PhysicsWorld* world, const storage::Store& store, storage::EntityKey key,
                             SnapshotStatus* detail)
{
    if (detail != nullptr)
    {
        *detail = SnapshotStatus::Ok;
    }
    storage::EntityRecord record;
    const storage::Status got = store.Get(key, &record);
    if (got.Failed())
    {
        return got;
    }
    const SnapshotStatus restored = world->Restore(ByteSpan(record.blob.Data(), record.blob.Size()));
    if (detail != nullptr)
    {
        *detail = restored;
    }
    return restored == SnapshotStatus::Ok
               ? storage::Status::Success()
               : storage::Status::Make(storage::ErrorCode::Corrupt, "the record is not a valid physics snapshot");
}

}  // namespace vse::physics
