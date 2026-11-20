#pragma once

// Compares two worlds through the registry (Phase 19 Day 260): the keyed entities of both, matched by key, must have
// the same components with the same encoded bytes. Entity ids and archetype layout are not compared here (the tests
// that care compare them on their own).

#include <catch2/catch_test_macros.hpp>

#include "ecs/query.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "storage/component_registry.h"
#include "storage/identity.h"

namespace vse::storage::test
{

struct EntityImage
{
    EntityKey key;
    ecs::Entity entity;
};

inline Vector<EntityImage> KeyedEntities(ecs::World& world)
{
    Vector<EntityImage> out;
    world.Query<ecs::Read<EntityKeyComponent>>().ForEach([&](ecs::Entity e, const EntityKeyComponent& k)
                                                         { out.Push(EntityImage{k.key, e}); });
    return out;
}

// The bytes of every stored component of the entity, in registration order, as "id:length:bytes" concatenated;
// presence-only components as "id:P".
inline bool ImageOf(const ComponentRegistry& registry, const ecs::World& world, ecs::Entity e, bytes::ByteWriter& out)
{
    const CodecLimits limits;
    for (u32 i = 0; i < registry.Count(); ++i)
    {
        const RegisteredComponent& c = registry.At(i);
        if (c.EcsId() == ecs::kInvalidComponent || !world.HasById(e, c.EcsId()))
        {
            continue;
        }
        if (c.Id() == kEntityKeyId || c.Id() == kParentLinkId || c.Id() == kOpaqueComponentsId)
        {
            continue;
        }
        out.WriteU32(c.Id());
        if (!c.IsStored())
        {
            out.WriteU32(0xFFFFFFFFu);
            continue;
        }
        bytes::ByteWriter one;
        if (c.Encode(world.CellOf(e, c.EcsId()), one, limits).Failed())
        {
            return false;
        }
        out.WriteU32(static_cast<u32>(one.Size()));
        out.WriteBytes(one.Bytes());
    }
    return true;
}

// Also compares the parent of each entity, by the key of the parent.
inline void RequireSameWorlds(const ComponentRegistry& registry, ecs::World& a, ecs::World& b)
{
    Vector<EntityImage> ea = KeyedEntities(a);
    Vector<EntityImage> eb = KeyedEntities(b);
    REQUIRE(ea.Size() == eb.Size());
    KeyIndex ib;
    REQUIRE(ib.Sync(b).Ok());
    KeyIndex ia;
    REQUIRE(ia.Sync(a).Ok());
    usize different = 0;
    for (usize i = 0; i < ea.Size(); ++i)
    {
        const ecs::Entity other = ib.Find(ea[i].key);
        if (!other.IsValid())
        {
            ++different;
            continue;
        }
        bytes::ByteWriter wa, wb;
        const bool ok = ImageOf(registry, a, ea[i].entity, wa) && ImageOf(registry, b, other, wb);
        const ecs::Parent* pa = a.Peek<ecs::Parent>(ea[i].entity);
        const ecs::Parent* pb = b.Peek<ecs::Parent>(other);
        const EntityKey ka = pa != nullptr ? ia.KeyOf(pa->entity) : EntityKey{};
        const EntityKey kb = pb != nullptr ? ib.KeyOf(pb->entity) : EntityKey{};
        const ByteSpan sa = wa.Bytes();
        const ByteSpan sb = wb.Bytes();
        bool same = ok && sa.Size() == sb.Size() && ka == kb && (pa != nullptr) == (pb != nullptr);
        for (usize k = 0; same && k < sa.Size(); ++k)
        {
            same = sa[k] == sb[k];
        }
        different += same ? 0 : 1;
    }
    CHECK(different == 0);
}

}  // namespace vse::storage::test
