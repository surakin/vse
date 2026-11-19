#include "renderer/device_resources.h"

#include "algorithm.h"
#include "cstr/format.h"

#include "log/assert.h"
#include "renderer/renderer_log.h"

namespace engine_placeholder::renderer
{

bool DeviceResources::Register(Recreatable* owner, u32 tier)
{
    if (resetting_)
    {
        RenderLog(LogVerbosity::Error, "DeviceResources: '%s' cannot register during a reset",
                  owner->RecreatableName());
        return false;
    }
    Entry e;
    e.tier = tier;
    e.sequence = next_sequence_++;
    e.live = owner->Live();
    owners_.Insert(owner, e);
    return true;
}

void DeviceResources::Compact()
{
    Vector<Recreatable*> dead;
    for (const auto& [owner, e] : owners_)
    {
        if (e.dead)
        {
            dead.Push(owner);
        }
    }
    for (Recreatable* owner : dead)
    {
        owners_.Remove(owner);
    }
    moved_.Clear();  // the step is over: no visit list names an old address
    compact_pending_ = false;
}

void DeviceResources::Unregister(Recreatable* owner)
{
    Entry* e = owner != nullptr ? owners_.Find(owner) : nullptr;
    if (e == nullptr || e->dead)
    {
        return;
    }
    if (resetting_)
    {
        e->dead = true;  // removed when the reset step ends; the loops skip it
        e->live = false;
        compact_pending_ = true;
        return;
    }
    owners_.Remove(owner);
}

void DeviceResources::Rebind(Recreatable* from, Recreatable* to)
{
    const Entry* e = from != nullptr ? owners_.Find(from) : nullptr;
    if (e == nullptr || e->dead)
    {
        return;
    }
    const Entry moved = *e;
    owners_.Remove(from);
    if (to != nullptr)
    {
        owners_.Insert(to, moved);
        if (resetting_)
        {
            moved_.Remove(to);        // `to` is the current address now: no move starts from it (no cycle)
            moved_.Insert(from, to);  // the step's visit list still names `from`: it follows the move
        }
    }
}

Recreatable* DeviceResources::Current(Recreatable* owner) const
{
    for (const Recreatable* const* to = moved_.Find(owner); to != nullptr; to = moved_.Find(owner))
    {
        owner = const_cast<Recreatable*>(*to);
    }
    return owner;
}

void DeviceResources::Order(u32 first_tier, bool ascending, Vector<Visit>* out) const
{
    out->Clear();
    for (const auto& [owner, e] : owners_)
    {
        if (!e.dead && e.tier >= first_tier)
        {
            out->Push(Visit{owner, e.tier, e.sequence});
        }
    }
    // (tier, sequence) is unique, so the order does not depend on the map's.
    Sort(*out,
         [ascending](const Visit& x, const Visit& y)
         {
             const bool before = x.tier < y.tier || (x.tier == y.tier && x.sequence < y.sequence);
             return ascending ? before : (x.tier > y.tier || (x.tier == y.tier && x.sequence > y.sequence));
         });
}

void DeviceResources::ReleaseAllLive()
{
    ReleaseLive(0);
    device_ = nullptr;
}

void DeviceResources::ReleaseFrom(u32 first_tier)
{
    resetting_ = true;
    ReleaseLive(first_tier);
    resetting_ = false;
    Compact();
}

bool DeviceResources::RecreateFrom(u32 first_tier)
{
    ENGINE_ASSERT(device_ != nullptr, "DeviceResources::RecreateFrom: there is no current device");
    resetting_ = true;
    failed_name_[0] = '\0';
    bool ok = true;
    Vector<Visit> order;
    Order(first_tier, true, &order);  // lowest tier first; within a tier the earlier registration first
    for (const Visit& v : order)
    {
        Recreatable* const owner = Current(v.owner);  // a Rebind during the step moved it
        Entry* e = owners_.Find(owner);
        if (e == nullptr || e->dead || e->live)
        {
            continue;  // unregistered meanwhile, or already live
        }
        e->live = true;
        if (!owner->Recreate(device_))
        {
            Recreatable* const now = Current(owner);
            FormatTo(failed_name_, sizeof(failed_name_), "%s", now->RecreatableName());
            RenderLog(LogVerbosity::Error, "DeviceResources: '%s' could not be recreated", failed_name_);
            e = owners_.Find(now);
            if (e != nullptr)
            {
                e->live = false;
            }
            ok = false;
            break;
        }
    }
    if (!ok)
    {
        ReleaseLive(first_tier);
    }
    resetting_ = false;
    Compact();
    return ok;
}

void DeviceResources::ReleaseLive(u32 first_tier)
{
    // Highest tier first; within a tier the later registration first.
    Vector<Visit> order;
    Order(first_tier, false, &order);
    for (const Visit& v : order)
    {
        Recreatable* const owner = Current(v.owner);  // a Rebind during the step moved it
        Entry* e = owners_.Find(owner);
        if (e == nullptr || e->dead || !e->live)
        {
            continue;  // unregistered meanwhile, or not live
        }
        e->live = false;
        owner->Release();
    }
}

void DeviceResources::OnDeviceReleasing(u32 epoch)
{
    (void)epoch;
    resetting_ = true;
    ReleaseAllLive();
    resetting_ = false;
    Compact();
}

bool DeviceResources::OnDeviceCreated(rhi::Device* device, u32 epoch)
{
    (void)epoch;
    device_ = device;
    if (!RecreateFrom(0))
    {
        device_ = nullptr;  // everything is released again; a half-built set is never used
        return false;
    }
    return true;
}

}  // namespace engine_placeholder::renderer
