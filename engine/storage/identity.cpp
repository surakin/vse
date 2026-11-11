#include "storage/identity.h"

#include "ecs/transform.h"
#include "os/os.h"

namespace engine_placeholder::storage
{

namespace
{

u64 SplitMix(u64* state)
{
    *state += 0x9E3779B97F4A7C15ull;
    u64 z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

int HexValue(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

// What ecs::SetParent does, without its pass over every parent link: the cost of a load must not grow with the square
// of the hierarchy. The children are attached after their parent (parents first), so the child has no descendants yet;
// a descendant already in a world that is merged into catches up through the transform propagation, which recomputes a
// stored depth that disagrees with the chain (docs/guide/ecs.md).
bool LinkParent(ecs::World& world, ecs::Entity child, ecs::Entity parent)
{
    if (!world.Exists(child) || !world.Exists(parent) || child == parent)
    {
        return false;
    }
    u32 parent_depth = 0;
    for (ecs::Entity up = parent;;)
    {
        const ecs::Parent* above = world.Peek<ecs::Parent>(up);
        if (above == nullptr)
        {
            break;
        }
        up = above->entity;
        ++parent_depth;
        if (up == child || parent_depth > ecs::kMaxHierarchyDepth)
        {
            return false;  // a cycle with the parents already in the world, or deeper than the limit
        }
    }
    if (parent_depth + 1 > ecs::kMaxHierarchyDepth)
    {
        return false;
    }
    const bool had_parent = world.Has<ecs::Parent>(child);
    if (world.Set<ecs::Parent>(child, ecs::Parent{parent}) != ecs::Status::Ok)
    {
        return false;
    }
    if (world.Set<ecs::HierarchyDepth>(child, ecs::HierarchyDepth{parent_depth + 1}) != ecs::Status::Ok)
    {
        if (!had_parent)
        {
            static_cast<void>(world.Remove<ecs::Parent>(child));  // no half-made link
        }
        return false;
    }
    return true;
}

}  // namespace

String<HeapAllocator> EntityKey::ToText() const
{
    String<HeapAllocator> text(HeapAllocator::Default());
    for (int half = 0; half < 2; ++half)
    {
        const u64 v = half == 0 ? high : low;
        for (int i = 15; i >= 0; --i)
        {
            text.Append("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
        }
    }
    return text;
}

bool EntityKey::FromText(StringView text, EntityKey* out)
{
    if (text.Length() != 32)
    {
        return false;
    }
    u64 halves[2] = {0, 0};
    for (usize i = 0; i < 32; ++i)
    {
        const int d = HexValue(text[i]);
        if (d < 0)
        {
            return false;
        }
        halves[i / 16] = (halves[i / 16] << 4) | static_cast<u64>(d);
    }
    out->high = halves[0];
    out->low = halves[1];
    return true;
}

KeyGenerator KeyGenerator::Seeded(u64 seed)
{
    KeyGenerator g;
    g.seeded_ = true;
    g.state_ = seed;
    return g;
}

EntityKey KeyGenerator::Next()
{
    for (;;)
    {
        EntityKey key;
        if (seeded_)
        {
            key.high = SplitMix(&state_);
            key.low = SplitMix(&state_);
        }
        else
        {
            os::FillRandomBytes(&key.high, sizeof(key.high));
            os::FillRandomBytes(&key.low, sizeof(key.low));
        }
        if (!key.IsNull())
        {
            return key;
        }
    }
}

void KeyIndex::Drop(ecs::Entity entity)
{
    const EntityKey* key = by_entity_.Find(entity);
    if (key != nullptr)
    {
        const EntityKey k = *key;
        const ecs::Entity* owner = by_key_.Find(k);
        if (owner != nullptr && *owner == entity)
        {
            by_key_.Remove(k);
        }
        by_entity_.Remove(entity);
    }
    for (usize i = 0; i < duplicates_.Size(); ++i)
    {
        if (duplicates_[i] == entity)
        {
            duplicates_.Remove(i);
            break;
        }
    }
}

bool KeyIndex::Index(ecs::World& world, ecs::Entity entity, EntityKey key)
{
    if (key.IsNull())
    {
        Drop(entity);
        return true;
    }
    const EntityKey* old = by_entity_.Find(entity);
    if (old != nullptr && *old == key)
    {
        return true;
    }
    if (old != nullptr)
    {
        Drop(entity);
    }
    const ecs::Entity* holder = by_key_.Find(key);
    if (holder != nullptr && *holder != entity)
    {
        const EntityKeyComponent* theirs = world.Peek<EntityKeyComponent>(*holder);
        if (theirs != nullptr && theirs->key == key)
        {
            return false;  // a live entity with this key is indexed already
        }
        by_entity_.Remove(*holder);  // a stale mapping: its entity changed key or went without a removal reaching us
    }
    by_key_.Insert(key, entity);
    by_entity_.Insert(entity, key);
    return true;
}

Status KeyIndex::Sync(ecs::World& world)
{
    const usize removed = world.RemovedCount<EntityKeyComponent>();
    u64 largest = last_removal_;
    for (usize i = 0; i < removed; ++i)
    {
        const ecs::RemovedComponent<EntityKeyComponent> entry = world.RemovedAt<EntityKeyComponent>(i);
        if (entry.seq <= last_removal_)
        {
            continue;
        }
        largest = entry.seq > largest ? entry.seq : largest;
        Drop(entry.entity);
    }
    last_removal_ = largest;

    struct Changed
    {
        ecs::Entity entity;
        EntityKey key;
    };
    Vector<Changed> changed;
    auto query = world.Query<ecs::Changed<EntityKeyComponent>, ecs::Read<EntityKeyComponent>>();
    query.SetSince(since_);
    query.ForEach([&](ecs::Entity e, const EntityKeyComponent& c) { changed.Push(Changed{e, c.key}); });
    query.Finish();
    since_ = query.Since();
    for (usize i = 0; i < changed.Size(); ++i)
    {
        if (!Index(world, changed[i].entity, changed[i].key))
        {
            Drop(changed[i].entity);
            duplicates_.Push(changed[i].entity);
        }
    }

    // An entity left out earlier may be indexable now: the holder of its key went, or one of them changed key.
    Vector<ecs::Entity> waiting;
    for (usize i = 0; i < duplicates_.Size(); ++i)
    {
        waiting.Push(duplicates_[i]);
    }
    duplicates_.Clear();
    for (usize i = 0; i < waiting.Size(); ++i)
    {
        const EntityKeyComponent* c = world.Exists(waiting[i]) ? world.Peek<EntityKeyComponent>(waiting[i]) : nullptr;
        if (c != nullptr && !Index(world, waiting[i], c->key))
        {
            duplicates_.Push(waiting[i]);
        }
    }
    if (!duplicates_.Empty())
    {
        return Status::Make(ErrorCode::Duplicate, "two entities carry one key");
    }
    return Status::Success();
}

Status KeyIndex::Assign(ecs::World& world, ecs::Entity entity, EntityKey key)
{
    static_cast<void>(
        Sync(world));  // duplicates already there are not this call's error; Sync leaves the index consistent
    if (key.IsNull() || !world.Exists(entity))
    {
        return Status::Make(ErrorCode::Invalid, "a key must not be null and the entity must be alive");
    }
    const ecs::Entity holder = Find(key);
    if (holder.IsValid() && holder != entity)
    {
        return Status::Make(ErrorCode::Duplicate, "another entity has this key");
    }
    if (world.Set(entity, EntityKeyComponent{key}) != ecs::Status::Ok)
    {
        return Status::Make(ErrorCode::Other, "the world refused the key component");
    }
    return Sync(world);
}

ecs::Entity KeyIndex::Find(EntityKey key) const
{
    const ecs::Entity* e = by_key_.Find(key);
    return e != nullptr ? *e : ecs::Entity{};
}

EntityKey KeyIndex::KeyOf(ecs::Entity entity) const
{
    const EntityKey* k = by_entity_.Find(entity);
    return k != nullptr ? *k : EntityKey{};
}

Status ResolveReference(const KeyIndex& index, EntityKey key, MissingPolicy policy, ReferenceReport* report,
                        ecs::Entity* out)
{
    *out = ecs::Entity{};
    if (key.IsNull())
    {
        return Status::Success();
    }
    const ecs::Entity found = index.Find(key);
    if (found.IsValid())
    {
        *out = found;
        return Status::Success();
    }
    if (report != nullptr)
    {
        ++report->missing;
        if (report->first_missing.Size() < ReferenceReport::kMaxReported)
        {
            report->first_missing.Push(key);
        }
        if (policy == MissingPolicy::NullReference)
        {
            ++report->nulled;
        }
    }
    if (policy == MissingPolicy::NullReference)
    {
        return Status::Success();
    }
    return Status::Make(ErrorCode::MissingReference, "a reference names an entity that is not there");
}

Status CaptureParents(ecs::World& world, const KeyIndex& index, ParentReport* report)
{
    struct Item
    {
        ecs::Entity child;
        EntityKey parent;
    };
    Vector<Item> links;
    Vector<ecs::Entity> stale;
    usize unkeyed = 0;
    world.Query<ecs::Read<ecs::Parent>>().ForEach(
        [&](ecs::Entity child, const ecs::Parent& p)
        {
            const EntityKey key = index.KeyOf(p.entity);
            if (key.IsNull())
            {
                ++unkeyed;
                stale.Push(child);
            }
            else
            {
                links.Push(Item{child, key});
            }
        });
    world.Query<ecs::Read<ParentLink>, ecs::Without<ecs::Parent>>().ForEach([&](ecs::Entity e, const ParentLink&)
                                                                            { stale.Push(e); });
    for (usize i = 0; i < stale.Size(); ++i)
    {
        static_cast<void>(world.Remove<ParentLink>(stale[i]));  // MissingComponent is fine: there was no link to remove
    }
    for (usize i = 0; i < links.Size(); ++i)
    {
        if (world.Set(links[i].child, ParentLink{EntityRef{links[i].parent}}) != ecs::Status::Ok)
        {
            return Status::Make(ErrorCode::Other, "the world refused a parent link");
        }
    }
    if (report != nullptr)
    {
        report->captured += links.Size();
        report->unkeyed += unkeyed;
    }
    return Status::Success();
}

Status ApplyParents(ecs::World& world, const KeyIndex& index, MissingPolicy policy, ParentReport* report)
{
    struct Item
    {
        ecs::Entity child;
        ecs::Entity parent;  // invalid: a root (null or missing reference)
        bool done = false;
    };
    Vector<Item> items;
    ReferenceReport refs;
    Status first_error = Status::Success();
    world.Query<ecs::Read<ParentLink>>().ForEach(
        [&](ecs::Entity child, const ParentLink& link)
        {
            Item item;
            item.child = child;
            const Status s = ResolveReference(index, link.parent.key, policy, &refs, &item.parent);
            if (s.Failed() && first_error.Ok())
            {
                first_error = s;
            }
            items.Push(item);
        });
    if (report != nullptr)
    {
        report->references.missing += refs.missing;
        report->references.nulled += refs.nulled;
        for (usize i = 0;
             i < refs.first_missing.Size() && report->references.first_missing.Size() < ReferenceReport::kMaxReported;
             ++i)
        {
            report->references.first_missing.Push(refs.first_missing[i]);
        }
    }
    if (first_error.Failed())
    {
        return first_error;  // nothing has changed
    }
    // Parents first, in linear time: each item waits on its parent's item (if that has a link too) and is released when
    // the parent's is done.
    constexpr u32 kNone = ~0u;
    HashMap<ecs::Entity, u32, HeapAllocator> item_of(HeapAllocator::Default());  // child -> its item number
    for (usize i = 0; i < items.Size(); ++i)
    {
        item_of.Insert(items[i].child, static_cast<u32>(i));
    }
    Vector<u32> first_dependent;
    Vector<u32> next_dependent;
    first_dependent.Resize(items.Size());
    next_dependent.Resize(items.Size());
    Vector<u32> ready;
    for (usize i = 0; i < items.Size(); ++i)
    {
        first_dependent[i] = kNone;
        next_dependent[i] = kNone;
    }
    for (usize i = 0; i < items.Size(); ++i)
    {
        const u32* parent_item = items[i].parent.IsValid() ? item_of.Find(items[i].parent) : nullptr;
        if (parent_item == nullptr)
        {
            ready.Push(static_cast<u32>(i));
        }
        else
        {
            next_dependent[i] = first_dependent[*parent_item];
            first_dependent[*parent_item] = static_cast<u32>(i);
        }
    }
    usize applied = 0;
    for (usize r = 0; r < ready.Size(); ++r)
    {  // `ready` grows while it is walked
        const u32 number = ready[r];
        Item& item = items[number];
        item.done = true;
        if (item.parent.IsValid())
        {
            if (LinkParent(world, item.child, item.parent))
            {
                ++applied;
            }
            else if (report != nullptr)
            {
                ++report->rejected;
            }
        }
        for (u32 d = first_dependent[number]; d != kNone; d = next_dependent[d])
        {
            ready.Push(d);
        }
    }
    for (usize i = 0; i < items.Size(); ++i)
    {
        if (!items[i].done && report != nullptr)
        {
            ++report->rejected;  // still waiting: in a cycle of links
        }
        static_cast<void>(world.Remove<ParentLink>(items[i].child));
    }
    if (report != nullptr)
    {
        report->applied += applied;
    }
    return Status::Success();
}

}  // namespace engine_placeholder::storage
