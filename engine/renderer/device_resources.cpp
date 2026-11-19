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
    e.owner = owner;
    e.tier = tier;
    e.sequence = next_sequence_++;
    e.live = owner->Live();
    index_.Insert(owner, static_cast<u32>(entries_.Size()));
    entries_.Push(e);
    return true;
}

// Removes the entry at `index` in O(1): the last entry takes its place (the order lives in `sequence`).
void DeviceResources::RemoveAt(usize index)
{
    const usize last = entries_.Size() - 1;
    if (index != last && entries_[last].owner != nullptr)
    {
        *index_.Find(entries_[last].owner) = static_cast<u32>(index);
    }
    entries_.SwapRemove(index);
}

void DeviceResources::Compact()
{
    for (usize i = entries_.Size(); i > 0; --i)
    {
        if (entries_[i - 1].owner == nullptr)
        {
            RemoveAt(i - 1);
        }
    }
    compact_pending_ = false;
}

void DeviceResources::Unregister(Recreatable* owner)
{
    const u32* at = owner != nullptr ? index_.Find(owner) : nullptr;
    if (at == nullptr)
    {
        return;
    }
    const usize i = *at;
    index_.Remove(owner);
    if (resetting_)
    {
        entries_[i].owner = nullptr;  // removed when the reset step ends; the loops skip it
        entries_[i].live = false;
        compact_pending_ = true;
        return;
    }
    RemoveAt(i);
}

void DeviceResources::Rebind(Recreatable* from, Recreatable* to)
{
    const u32* at = from != nullptr ? index_.Find(from) : nullptr;
    if (at == nullptr)
    {
        return;
    }
    const u32 i = *at;
    index_.Remove(from);
    entries_[i].owner = to;
    if (to != nullptr)
    {
        index_.Insert(to, i);
    }
}

void DeviceResources::Order(u32 first_tier, bool ascending, Vector<u32>* out) const
{
    out->Clear();
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        if (entries_[i].owner != nullptr && entries_[i].tier >= first_tier)
        {
            out->Push(static_cast<u32>(i));
        }
    }
    const Vector<Entry>& entries = entries_;
    Sort(*out,
         [&](u32 a, u32 b)
         {
             const Entry& x = entries[a];
             const Entry& y = entries[b];
             const bool before = x.tier < y.tier || (x.tier == y.tier && x.sequence < y.sequence);
             return ascending ? before : (!before && (x.tier != y.tier || x.sequence != y.sequence));
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
    Vector<u32> order;
    Order(first_tier, true, &order);  // lowest tier first; within a tier the earlier registration first
    for (u32 at : order)
    {
        Entry& e = entries_[at];
        if (e.owner == nullptr || e.live)
        {
            continue;  // unregistered meanwhile, or already live
        }
        e.live = true;
        if (!e.owner->Recreate(device_))
        {
            FormatTo(failed_name_, sizeof(failed_name_), "%s", e.owner->RecreatableName());
            RenderLog(LogVerbosity::Error, "DeviceResources: '%s' could not be recreated", failed_name_);
            e.live = false;
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
    Vector<u32> order;
    Order(first_tier, false, &order);
    for (u32 at : order)
    {
        Entry& e = entries_[at];
        if (e.owner == nullptr || !e.live)
        {
            continue;  // unregistered meanwhile, or not live
        }
        e.live = false;
        e.owner->Release();
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
