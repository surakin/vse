#include "script_physics/character_set.h"

#include "ecs/transform.h"
#include "lang/scalar.h"
#include "lang/utility.h"

namespace engine_placeholder::script_physics
{

namespace
{

bool FiniteVector(const Vec3& v)
{
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

}  // namespace

CharacterSet::CharacterSet(physics::PhysicsWorld* world) : world_(world)
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

bool CharacterSet::Add(ecs::Entity entity, const physics::CharacterConfig& config, const Vec3& position)
{
    if (!entity.IsValid() || entity.IsPlaceholder() || FindEntry(entity) != nullptr || !FiniteVector(position))
    {
        return false;
    }
    Entry entry;
    entry.key = entity.ToU64();
    entry.entity = entity;
    if (entry.controller.Create(world_, config, position) != physics::BodyStatus::Ok)
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

ecs::Entity CharacterSet::EntityOfBody(physics::BodyId body) const
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

const physics::CharacterController* CharacterSet::Find(ecs::Entity entity) const
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

void CharacterSet::PreStep(void* user, physics::PhysicsWorld* world, f32 dt)
{
    CharacterSet* set = static_cast<CharacterSet*>(user);
    for (usize i = 0; i < set->entries_.Size(); ++i)
    {
        Entry& entry = set->entries_[i];
        if (entry.resize && entry.controller.SetHeight(world, entry.height) == physics::BodyStatus::Ok)
        {
            entry.resize = false;  // a blocked stand-up (a ceiling) is asked again at the next step
        }
        if (entry.jump)
        {
            entry.jump = false;
            entry.controller.Jump(entry.jump_speed);
        }
        entry.controller.Move(world, entry.wish, dt);
    }
}

u32 CharacterSet::WriteBack(ecs::World& ecs) const
{
    u32 written = 0;
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        const Entry& entry = entries_[i];
        const physics::Body* body = world_->Find(entry.controller.BodyHandle());
        ecs::LocalTransform* transform = body != nullptr ? ecs.Get<ecs::LocalTransform>(entry.entity) : nullptr;
        if (transform != nullptr)
        {
            transform->position = body->position;
            ++written;
        }
    }
    return written;
}

}  // namespace engine_placeholder::script_physics
