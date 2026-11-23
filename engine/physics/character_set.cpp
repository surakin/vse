#include "physics/character_set.h"

#include "ecs/transform.h"
#include "lang/scalar.h"
#include "lang/utility.h"
#include "physics/snapshot_io.h"

namespace vse::physics
{

namespace
{

bool FiniteVector(const Vec3& v)
{
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

bool FiniteVector(const Vec3d& v)
{
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

// One character as `CharacterSet::Snapshot` keeps it.
struct KeptCharacter
{
    u64 key = 0;
    BodyId body;
    CharacterState state;
    Vec3 wish = Vec3(0.0f, 0.0f, 0.0f);
    bool jump = false;
    f32 jump_speed = 0.0f;
    bool resize = false;
    f32 height = 0.0f;
};

constexpr usize kKeptBytes = 63;  // the fixed size of one kept character

BodyId ReadBodyId(SnapshotIn& in)
{
    BodyId id;
    id.index = in.U32();
    id.generation = in.U32();
    return id;
}

// Reads a whole snapshot of the set; false for anything malformed.
bool ReadKept(ByteSpan data, Vector<KeptCharacter>* out)
{
    SnapshotIn in(data);
    const usize count = in.Count(in.Remaining() / kKeptBytes);
    for (usize i = 0; i < count && in.Ok(); ++i)
    {
        KeptCharacter k;
        k.key = in.U64();
        k.body = ReadBodyId(in);
        k.state.ground_body = ReadBodyId(in);
        k.state.on_ground = in.Bool();
        k.state.ground_normal = in.V3();
        k.state.vertical_speed = in.F32();
        k.wish = in.V3();
        k.jump = in.Bool();
        k.jump_speed = in.F32();
        k.resize = in.Bool();
        k.height = in.F32();
        const bool ordered = out->Empty() || (*out)[out->Size() - 1].key < k.key;
        if (!ordered || !FiniteVector(k.state.ground_normal) || !IsFinite(k.state.vertical_speed) ||
            !FiniteVector(k.wish) || !IsFinite(k.jump_speed) || !IsFinite(k.height))
        {
            return false;
        }
        out->Push(k);
    }
    return in.Ok() && in.Remaining() == 0;
}

}  // namespace

CharacterSet::CharacterSet(PhysicsWorld* world) : world_(world)
{
    world_->SetPreStepHook(&CharacterSet::PreStep, this);
}

CharacterSet::~CharacterSet()
{
    world_->SetPreStepHook(nullptr, nullptr);
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        entries_[i].controller.Destroy(world_);
    }
}

usize CharacterSet::LowerBound(u64 key) const
{
    usize lo = 0;
    usize hi = entries_.Size();
    while (lo < hi)
    {
        const usize mid = (lo + hi) / 2;
        if (entries_[mid].key < key)
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

CharacterSet::Entry* CharacterSet::FindEntry(ecs::Entity entity)
{
    if (!entity.IsValid() || entity.IsPlaceholder())
    {
        return nullptr;
    }
    const u64 key = entity.ToU64();
    const usize at = LowerBound(key);
    return at < entries_.Size() && entries_[at].key == key ? &entries_[at] : nullptr;
}

const CharacterSet::Entry* CharacterSet::FindEntry(ecs::Entity entity) const
{
    if (!entity.IsValid() || entity.IsPlaceholder())
    {
        return nullptr;
    }
    const u64 key = entity.ToU64();
    const usize at = LowerBound(key);
    return at < entries_.Size() && entries_[at].key == key ? &entries_[at] : nullptr;
}

bool CharacterSet::Add(ecs::Entity entity, const CharacterConfig& config, const Vec3d& position)
{
    if (!entity.IsValid() || entity.IsPlaceholder() || FindEntry(entity) != nullptr || !FiniteVector(position))
    {
        return false;
    }
    Entry entry;
    entry.key = entity.ToU64();
    entry.entity = entity;
    if (entry.controller.Create(world_, config, position) != BodyStatus::Ok)
    {
        return false;
    }
    entries_.Insert(LowerBound(entry.key), Move(entry));
    return true;
}

bool CharacterSet::Remove(ecs::Entity entity)
{
    Entry* entry = FindEntry(entity);
    if (entry == nullptr)
    {
        return false;
    }
    entry->controller.Destroy(world_);
    entries_.Remove(static_cast<usize>(entry - entries_.Data()));
    return true;
}

u32 CharacterSet::Prune(const ecs::World& ecs)
{
    u32 removed = 0;
    for (usize i = entries_.Size(); i > 0; --i)
    {
        if (!ecs.Exists(entries_[i - 1].entity))
        {
            entries_[i - 1].controller.Destroy(world_);
            entries_.Remove(i - 1);
            ++removed;
        }
    }
    return removed;
}

bool CharacterSet::SetMove(ecs::Entity entity, const Vec3& velocity)
{
    Entry* entry = FindEntry(entity);
    if (entry == nullptr || !FiniteVector(velocity))
    {
        return false;
    }
    entry->wish = Vec3(velocity.x, 0.0f, velocity.z);
    return true;
}

bool CharacterSet::Jump(ecs::Entity entity, f32 speed)
{
    Entry* entry = FindEntry(entity);
    if (entry == nullptr || !IsFinite(speed))
    {
        return false;
    }
    entry->jump = true;
    entry->jump_speed = speed;
    return true;
}

bool CharacterSet::SetHeight(ecs::Entity entity, f32 height)
{
    Entry* entry = FindEntry(entity);
    if (entry == nullptr || !IsFinite(height) || !(height >= 2.0f * entry->controller.Config().radius))
    {
        return false;
    }
    entry->resize = true;
    entry->height = height;
    return true;
}

ecs::Entity CharacterSet::EntityOfBody(BodyId body) const
{
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        if (entries_[i].controller.BodyHandle() == body)
        {
            return entries_[i].entity;
        }
    }
    return ecs::Entity();
}

const CharacterController* CharacterSet::Find(ecs::Entity entity) const
{
    const Entry* entry = FindEntry(entity);
    return entry != nullptr ? &entry->controller : nullptr;
}

bool CharacterSet::Wish(ecs::Entity entity, Vec3* velocity) const
{
    const Entry* entry = FindEntry(entity);
    *velocity = entry != nullptr ? entry->wish : Vec3(0.0f, 0.0f, 0.0f);
    return entry != nullptr;
}

void CharacterSet::PreStep(void* user, PhysicsWorld* world, f32 dt)
{
    CharacterSet* set = static_cast<CharacterSet*>(user);
    for (usize i = 0; i < set->entries_.Size(); ++i)
    {
        Entry& entry = set->entries_[i];
        if (entry.resize && entry.controller.SetHeight(world, entry.height) == BodyStatus::Ok)
        {
            entry.resize = false;  // a blocked stand-up (a ceiling) is asked again at the next step
        }
        if (entry.jump)
        {
            entry.jump = false;
            entry.controller.Jump(entry.jump_speed);
        }
        entry.events = entry.controller.Move(world, entry.wish, dt);
    }
}

u32 CharacterSet::WriteBack(ecs::World& ecs, bool interpolate) const
{
    if (!interpolate)
    {
        return WriteBack(ecs);
    }
    u32 written = 0;
    const f32 alpha = world_->Alpha();
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        const Entry& entry = entries_[i];
        Vec3d position;
        Quaternion rotation;
        if (!world_->Interpolated(entry.controller.BodyHandle(), alpha, &position, &rotation))
        {
            continue;
        }
        if (ecs::LocalTransform* transform = ecs.Get<ecs::LocalTransform>(entry.entity))
        {
            transform->position = position;
            ++written;
        }
    }
    return written;
}

bool CharacterSet::SetPosition(ecs::Entity entity, const Vec3d& position)
{
    Entry* entry = FindEntry(entity);
    if (entry == nullptr || !FiniteVector(position) ||
        world_->SetTransform(entry->controller.BodyHandle(), position, Quaternion::Identity()) != BodyStatus::Ok)
    {
        return false;
    }
    entry->controller.Stop(world_);
    // In the air until the next step looks: a platform the character stood on must not carry it from its new place.
    entry->controller.RestoreState(entry->controller.BodyHandle(), CharacterState());
    entry->jump = false;
    entry->events = CharacterEvents();
    return true;
}

const CharacterEvents* CharacterSet::LastEvents(ecs::Entity entity) const
{
    const Entry* entry = FindEntry(entity);
    return entry != nullptr ? &entry->events : nullptr;
}

u32 CharacterSet::Recreate(const ecs::World& ecs)
{
    u32 made = 0;
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        Entry& entry = entries_[i];
        if (world_->Find(entry.controller.BodyHandle()) != nullptr)
        {
            continue;
        }
        const ecs::LocalTransform* transform = ecs.Peek<ecs::LocalTransform>(entry.entity);
        if (transform != nullptr &&
            entry.controller.Create(world_, entry.controller.Config(), transform->position) == BodyStatus::Ok)
        {
            ++made;
        }
    }
    return made;
}

u32 CharacterSet::WriteBack(ecs::World& ecs) const
{
    u32 written = 0;
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        const Entry& entry = entries_[i];
        const Body* body = world_->Find(entry.controller.BodyHandle());
        ecs::LocalTransform* transform = body != nullptr ? ecs.Get<ecs::LocalTransform>(entry.entity) : nullptr;
        if (transform != nullptr)
        {
            transform->position = body->position;
            ++written;
        }
    }
    return written;
}

void CharacterSet::Snapshot(bytes::ByteWriter* out) const
{
    SnapshotOut w(out);
    w.Count(entries_.Size());
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        const Entry& entry = entries_[i];
        const CharacterState& state = entry.controller.State();
        w.U64(entry.key);
        w.Id(entry.controller.BodyHandle().index, entry.controller.BodyHandle().generation);
        w.Id(state.ground_body.index, state.ground_body.generation);
        w.Bool(state.on_ground);
        w.V3(state.ground_normal);
        w.F32(state.vertical_speed);
        w.V3(entry.wish);
        w.Bool(entry.jump);
        w.F32(entry.jump_speed);
        w.Bool(entry.resize);
        w.F32(entry.height);
    }
}

bool CharacterSet::ValidSnapshot(ByteSpan data)
{
    Vector<KeptCharacter> kept;
    return ReadKept(data, &kept);
}

bool CharacterSet::Restore(ByteSpan data)
{
    Vector<KeptCharacter> kept;
    if (!ReadKept(data, &kept))
    {
        return false;
    }
    for (usize i = 0; i < kept.Size(); ++i)
    {
        const KeptCharacter& k = kept[i];
        const Body* body = world_->Find(k.body);
        const bool body_ok = body != nullptr && body->settings.type == BodyType::Kinematic;
        const usize at = LowerBound(k.key);
        if (at >= entries_.Size() || entries_[at].key != k.key)
        {
            if (body_ok)
            {
                world_->DestroyBody(k.body);  // the character is gone since the snapshot: its body goes too
            }
            continue;
        }
        Entry& entry = entries_[at];
        entry.controller.RestoreState(body_ok ? k.body : BodyId(), k.state);
        entry.wish = k.wish;
        entry.jump = k.jump;
        entry.jump_speed = k.jump_speed;
        entry.resize = k.resize;
        entry.height = k.height;
        entry.events = CharacterEvents();
    }
    return true;
}

}  // namespace vse::physics
