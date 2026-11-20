#include "physics/input_log.h"

#include "physics/snapshot_io.h"

namespace vse::physics
{

namespace
{

constexpr u32 kLogMagic = 0x474c5048u;  // "HPLG"
constexpr u32 kLogVersion = 1;

void WriteDesc(SnapshotOut& out, const BodyDesc& d)
{
    out.U8(static_cast<u32>(d.settings.type));
    out.U32(d.settings.flags);
    out.F32(d.settings.mass);
    out.F32(d.settings.linear_damping);
    out.F32(d.settings.angular_damping);
    out.F32(d.settings.gravity_scale);
    out.V3(d.position);
    out.Q(d.orientation);
    out.V3(d.velocity.linear);
    out.V3(d.velocity.angular);
    out.Id(d.entity.index, d.entity.generation);
}

void ReadDesc(SnapshotIn& in, BodyDesc* d)
{
    d->settings.type = static_cast<BodyType>(in.U8());
    d->settings.flags = in.U32();
    d->settings.mass = in.F32();
    d->settings.linear_damping = in.F32();
    d->settings.angular_damping = in.F32();
    d->settings.gravity_scale = in.F32();
    d->position = in.V3();
    d->orientation = in.Q();
    d->velocity.linear = in.V3();
    d->velocity.angular = in.V3();
    d->entity.index = in.U32();
    d->entity.generation = in.U32();
}

}  // namespace

bool InputLog::Add(const InputEntry& entry)
{
    if (!entries_.Empty() && entry.step < entries_[entries_.Size() - 1].step)
    {
        return false;
    }
    entries_.Push(entry);
    return true;
}

void InputLog::Save(bytes::ByteWriter* writer) const
{
    SnapshotOut out(writer);
    out.U32(kLogMagic);
    out.U32(kLogVersion);
    out.Count(entries_.Size());
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        const InputEntry& e = entries_[i];
        out.U64(e.step);
        out.U8(static_cast<u32>(e.kind));
        out.Id(e.body.index, e.body.generation);
        out.V3(e.a);
        out.V3(e.b);
        out.Q(e.q);
        WriteDesc(out, e.desc);
        out.Bool(e.has_collider);
        WriteColliderValue(out, e.collider);
    }
}

SnapshotStatus InputLog::Load(ByteSpan data)
{
    entries_.Clear();
    SnapshotIn in(data);
    const u32 magic = in.U32();
    if (!in.Ok())
    {
        return SnapshotStatus::Truncated;
    }
    if (magic != kLogMagic)
    {
        return SnapshotStatus::BadMagic;
    }
    const u32 version = in.U32();
    if (!in.Ok())
    {
        return SnapshotStatus::Truncated;
    }
    if (version != kLogVersion)
    {
        return SnapshotStatus::UnknownVersion;
    }
    // An entry is at least 150 bytes.
    const usize count = in.Count(in.Remaining() / 150);
    for (usize i = 0; i < count && in.Ok(); ++i)
    {
        InputEntry e;
        e.step = in.U64();
        const u32 kind = in.U8();
        e.body.index = in.U32();
        e.body.generation = in.U32();
        e.a = in.V3();
        e.b = in.V3();
        e.q = in.Q();
        ReadDesc(in, &e.desc);
        e.has_collider = in.Bool();
        ReadColliderValue(in, &e.collider);
        if (kind > static_cast<u32>(InputKind::DestroyBody) || !SnapshotFinite(e.a) || !SnapshotFinite(e.b) ||
            !SnapshotFinite(e.q) || !SnapshotFinite(e.desc.position) || !SnapshotFinite(e.desc.orientation) ||
            !SnapshotFinite(e.desc.velocity.linear) || !SnapshotFinite(e.desc.velocity.angular) ||
            (!entries_.Empty() && e.step < entries_[entries_.Size() - 1].step))
        {
            in.Fail();
            break;
        }
        e.kind = static_cast<InputKind>(kind);
        entries_.Push(e);
    }
    if (!in.Ok())
    {
        entries_.Clear();
        return in.Remaining() == 0 ? SnapshotStatus::Truncated : SnapshotStatus::Corrupt;
    }
    if (in.Remaining() != 0)
    {
        entries_.Clear();
        return SnapshotStatus::Corrupt;
    }
    return SnapshotStatus::Ok;
}

void InputRecorder::Log(InputEntry* entry)
{
    entry->step = world_->GetStepStats().steps;
    log_->Add(*entry);
}

BodyStatus InputRecorder::ApplyForce(BodyId id, const Vec3& force, const Vec3& point)
{
    const BodyStatus status = world_->ApplyForce(id, force, point);
    if (status == BodyStatus::Ok)
    {
        InputEntry e;
        e.kind = InputKind::ApplyForce;
        e.body = id;
        e.a = force;
        e.b = point;
        Log(&e);
    }
    return status;
}

BodyStatus InputRecorder::ApplyImpulse(BodyId id, const Vec3& impulse, const Vec3& point)
{
    const BodyStatus status = world_->ApplyImpulse(id, impulse, point);
    if (status == BodyStatus::Ok)
    {
        InputEntry e;
        e.kind = InputKind::ApplyImpulse;
        e.body = id;
        e.a = impulse;
        e.b = point;
        Log(&e);
    }
    return status;
}

BodyStatus InputRecorder::SetVelocity(BodyId id, const Velocity& velocity)
{
    const BodyStatus status = world_->SetVelocity(id, velocity);
    if (status == BodyStatus::Ok)
    {
        InputEntry e;
        e.kind = InputKind::SetVelocity;
        e.body = id;
        e.a = velocity.linear;
        e.b = velocity.angular;
        Log(&e);
    }
    return status;
}

BodyStatus InputRecorder::SetTransform(BodyId id, const Vec3& position, const Quaternion& orientation)
{
    const BodyStatus status = world_->SetTransform(id, position, orientation);
    if (status == BodyStatus::Ok)
    {
        InputEntry e;
        e.kind = InputKind::SetTransform;
        e.body = id;
        e.a = position;
        e.q = orientation;
        Log(&e);
    }
    return status;
}

BodyStatus InputRecorder::Wake(BodyId id)
{
    const BodyStatus status = world_->Wake(id);
    if (status == BodyStatus::Ok)
    {
        InputEntry e;
        e.kind = InputKind::Wake;
        e.body = id;
        Log(&e);
    }
    return status;
}

BodyId InputRecorder::CreateBody(const BodyDesc& desc, const Collider& collider, BodyStatus* status)
{
    BodyStatus local = BodyStatus::Ok;
    const BodyId id = world_->CreateBody(desc, &local);
    if (id.IsValid())
    {
        local = world_->AddCollider(id, collider);
        if (local != BodyStatus::Ok)
        {
            world_->DestroyBody(id);
        }
    }
    if (status != nullptr)
    {
        *status = local;
    }
    if (!id.IsValid() || local != BodyStatus::Ok)
    {
        return BodyId();
    }
    InputEntry e;
    e.kind = InputKind::CreateBody;
    e.body = id;
    e.desc = desc;
    e.has_collider = true;
    e.collider = collider;
    Log(&e);
    return id;
}

BodyStatus InputRecorder::DestroyBody(BodyId id)
{
    const BodyStatus status = world_->DestroyBody(id);
    if (status == BodyStatus::Ok)
    {
        InputEntry e;
        e.kind = InputKind::DestroyBody;
        e.body = id;
        Log(&e);
    }
    return status;
}

u32 ReplayStep(PhysicsWorld* world, const InputLog& log, usize* cursor)
{
    const u64 step = world->GetStepStats().steps;
    u32 applied = 0;
    const Vector<InputEntry>& entries = log.Entries();
    while (*cursor < entries.Size() && entries[*cursor].step <= step)
    {
        const InputEntry& e = entries[*cursor];
        ++*cursor;
        if (e.step < step)
        {
            continue;  // an entry for a step the world is already past
        }
        switch (e.kind)
        {
        case InputKind::ApplyForce: world->ApplyForce(e.body, e.a, e.b); break;
        case InputKind::ApplyImpulse: world->ApplyImpulse(e.body, e.a, e.b); break;
        case InputKind::SetVelocity:
        {
            Velocity v;
            v.linear = e.a;
            v.angular = e.b;
            world->SetVelocity(e.body, v);
            break;
        }
        case InputKind::SetTransform: world->SetTransform(e.body, e.a, e.q); break;
        case InputKind::Wake: world->Wake(e.body); break;
        case InputKind::CreateBody:
        {
            const BodyId id = world->CreateBody(e.desc);
            if (id.IsValid() && e.has_collider && world->AddCollider(id, e.collider) != BodyStatus::Ok)
            {
                world->DestroyBody(id);
            }
            break;
        }
        case InputKind::DestroyBody: world->DestroyBody(e.body); break;
        }
        ++applied;
    }
    world->Step();
    return applied;
}

usize FirstEntryAt(const InputLog& log, u64 step)
{
    const Vector<InputEntry>& entries = log.Entries();
    usize lo = 0;
    usize hi = entries.Size();
    while (lo < hi)
    {
        const usize mid = (lo + hi) / 2;
        if (entries[mid].step < step)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    return lo;
}

}  // namespace vse::physics
