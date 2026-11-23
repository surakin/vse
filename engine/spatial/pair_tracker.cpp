#include "spatial/pair_tracker.h"

#include "algorithm.h"
#include "lang/scalar.h"
#include "log/assert.h"

namespace vse::spatial
{

namespace
{

bool PairLess(const KeyPair& x, const KeyPair& y)
{
    return x.a != y.a ? x.a < y.a : x.b < y.b;
}
bool PairEqual(const KeyPair& x, const KeyPair& y)
{
    return x.a == y.a && x.b == y.b;
}

void SortUnique(Vector<KeyPair>* v)
{
    Sort(Span<KeyPair>(v->Data(), v->Size()), PairLess);
    usize out = 0;
    for (usize i = 0; i < v->Size(); ++i)
    {
        if (out == 0 || !PairEqual((*v)[out - 1], (*v)[i]))
        {
            (*v)[out++] = (*v)[i];
        }
    }
    v->Resize(out);
}

// The first index whose entry is not less than `key` in the first position.
usize FirstOf(const Vector<KeyPair>& v, SpatialKey key)
{
    usize lo = 0, hi = v.Size();
    while (lo < hi)
    {
        const usize mid = lo + (hi - lo) / 2;
        if (v[mid].a < key)
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

bool Contains(const Vector<KeyPair>& v, SpatialKey a, SpatialKey b)
{
    const KeyPair probe{a, b};
    usize lo = 0, hi = v.Size();
    while (lo < hi)
    {
        const usize mid = lo + (hi - lo) / 2;
        if (PairLess(v[mid], probe))
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    return lo < v.Size() && PairEqual(v[lo], probe);
}

f64 MarginOf(const Aabbd& box, f64 fraction)
{
    const Vec3d e = box.Extents();
    return fraction * Max(e.x, Max(e.y, e.z));
}

// Removes from both sorted lists the entries they share.
void CancelCommon(Vector<KeyPair>* x, Vector<KeyPair>* y)
{
    usize i = 0, j = 0, ox = 0, oy = 0;
    while (i < x->Size() && j < y->Size())
    {
        if (PairEqual((*x)[i], (*y)[j]))
        {
            ++i;
            ++j;
        }
        else if (PairLess((*x)[i], (*y)[j]))
        {
            (*x)[ox++] = (*x)[i++];
        }
        else
        {
            (*y)[oy++] = (*y)[j++];
        }
    }
    while (i < x->Size())
    {
        (*x)[ox++] = (*x)[i++];
    }
    while (j < y->Size())
    {
        (*y)[oy++] = (*y)[j++];
    }
    x->Resize(ox);
    y->Resize(oy);
}

}  // namespace

void PairTrackerConfig::SetMeets(u32 a, u32 b, bool value)
{
    ENGINE_ASSERT(a < 32 && b < 32, "PairTrackerConfig::SetMeets: a layer is 32 or more");
    if (value)
    {
        meets[a] |= 1u << b;
        meets[b] |= 1u << a;
    }
    else
    {
        meets[a] &= ~(1u << b);
        meets[b] &= ~(1u << a);
    }
}

PairTracker::PairTracker(const PairTrackerConfig& config) : config_(config)
{
    ENGINE_ASSERT(IsFinite(config.margin) && config.margin >= 0.0f,
                  "PairTracker: the margin must be finite and not negative");
}

bool PairTracker::Has(SpatialKey a, SpatialKey b) const
{
    return Contains(adjacent_, a, b);
}

PairTrackerStats PairTracker::Stats() const
{
    PairTrackerStats s = stats_;
    s.pairs = PairCount();
    return s;
}

// Whether the pair is held and neither key was removed since the last update (a removed key that came back is new: it
// has no pairs).
bool PairTracker::Held(SpatialKey a, SpatialKey b) const
{
    if (!Contains(adjacent_, a, b))
    {
        return false;
    }
    usize lo = 0, hi = removed_.Size();
    while (lo < hi)
    {
        const usize mid = lo + (hi - lo) / 2;
        if (removed_[mid] < a)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    if (lo < removed_.Size() && removed_[lo] == a)
    {
        return false;
    }
    lo = 0;
    hi = removed_.Size();
    while (lo < hi)
    {
        const usize mid = lo + (hi - lo) / 2;
        if (removed_[mid] < b)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    return !(lo < removed_.Size() && removed_[lo] == b);
}

void PairTracker::Clear()
{
    adjacent_.Clear();
    touched_.Clear();
    removed_.Clear();
    // The largest margins are kept: items still in the set may be larger than any touched later, and the bound only
    // ever needs to be too big.
}

// Finds, for each touched live item, the pairs to begin (into begin_) and, comparing with the pairs held, the ones to
// end (into end_). Entries are directed (both orientations are added).
void PairTracker::Gather(const SpatialSet& set, const Vector<SetHandle>& touched, bool use_margin)
{
    const f64 fraction = use_margin ? config_.margin : 0.0f;
    // The largest margin of any item ever touched bounds how far a partner's grown box can reach (counted with the
    // configured margin even when this call makes pairs without one, so a later `Update` finds partners the recompute
    // saw).
    for (usize i = 0; i < touched.Size(); ++i)
    {
        if (!set.IsLive(touched[i]))
        {
            continue;
        }
        const f64 m = MarginOf(set.ItemOf(touched[i]).box, config_.margin);
        f64& largest = set.IsStatic(touched[i]) ? largest_static_margin_ : largest_dynamic_margin_;
        if (m > largest)
        {
            largest = m;
        }
    }
    for (usize i = 0; i < touched.Size(); ++i)
    {
        const SetHandle handle = touched[i];
        if (!set.IsLive(handle))
        {
            continue;
        }
        const Item self = set.ItemOf(handle);
        const bool self_static = set.IsStatic(handle);
        const f64 self_margin = MarginOf(self.box, fraction);
        const Aabbd self_grown = Expand(self.box, self_margin);
        // The static partners reach as far as the largest static margin and the dynamic ones as far as the largest
        // dynamic margin: one huge static box (a floor) does not widen the search among the dynamic items.
        const Aabbd static_region = Expand(self.box, self_margin + largest_static_margin_);
        const Aabbd dynamic_region = Expand(self.box, self_margin + largest_dynamic_margin_);
        near_.Clear();
        ++stats_.touched;
        // The regions reach every partner: an item's margin is at most the largest of its class ever touched (an item
        // that changes class is touched again, which counts it in the new one).
        set.ForEachHandleInRegion(
            static_region, dynamic_region,  // through a grid's cells when the set has one: a scan of every item would
                                            // make the update quadratic
            [&](SetHandle other, const Item& item)
            {
                if (other == handle || item.key == self.key)
                {
                    return true;
                }
                if (!config_.Meets(self.layer, item.layer))
                {
                    return true;
                }
                if (self_static && set.IsStatic(other))
                {
                    return true;
                }
                if (!Overlaps(self_grown, Expand(item.box, MarginOf(item.box, fraction))))
                {
                    return true;
                }
                near_.Push(item.key);
                if (!Overlaps(self.box, item.box))
                {
                    return true;  // near, but they have not touched: a pair is made only on overlap
                }
                if (Held(self.key, item.key))
                {
                    return true;  // already a pair
                }
                begin_.Push(KeyPair{self.key, item.key});
                begin_.Push(KeyPair{item.key, self.key});
                return true;
            });
        // The pairs held for this key that are no longer near end.
        Sort(Span<SpatialKey>(near_.Data(), near_.Size()), [](SpatialKey x, SpatialKey y) { return x < y; });
        for (usize k = FirstOf(adjacent_, self.key); k < adjacent_.Size() && adjacent_[k].a == self.key; ++k)
        {
            const SpatialKey partner = adjacent_[k].b;
            usize lo = 0, hi = near_.Size();
            while (lo < hi)
            {
                const usize mid = lo + (hi - lo) / 2;
                if (near_[mid] < partner)
                {
                    lo = mid + 1;
                }
                else
                {
                    hi = mid;
                }
            }
            if (lo < near_.Size() && near_[lo] == partner)
            {
                continue;
            }
            end_.Push(KeyPair{self.key, partner});
            end_.Push(KeyPair{partner, self.key});
        }
    }
}

// Sorts the gathered lists, cancels what both name, applies them to the pair list and reports them.
void PairTracker::Finish(PairEvents* events)
{
    SortUnique(&begin_);
    SortUnique(&end_);
    CancelCommon(&begin_, &end_);
    // adjacent_ = (adjacent_ without end_) plus begin_, merged in order.
    merged_.Clear();
    merged_.Reserve(adjacent_.Size() + begin_.Size());
    usize i = 0, b = 0, e = 0;
    while (i < adjacent_.Size() || b < begin_.Size())
    {
        if (i < adjacent_.Size())
        {
            while (e < end_.Size() && PairLess(end_[e], adjacent_[i]))
            {
                ++e;
            }
            if (e < end_.Size() && PairEqual(end_[e], adjacent_[i]))
            {
                ++i;
                ++e;
                continue;
            }
        }
        if (b >= begin_.Size() || (i < adjacent_.Size() && PairLess(adjacent_[i], begin_[b])))
        {
            merged_.Push(adjacent_[i++]);
        }
        else
        {
            merged_.Push(begin_[b++]);
        }
    }
    adjacent_.Swap(merged_);  // merged_ is cleared before its next use (container audit Day 8: a copy every update)
    for (usize k = 0; k < begin_.Size(); ++k)
    {
        if (begin_[k].a < begin_[k].b)
        {
            events->begin.Push(begin_[k]);
        }
    }
    for (usize k = 0; k < end_.Size(); ++k)
    {
        if (end_[k].a < end_[k].b)
        {
            events->end.Push(end_[k]);
        }
    }
    stats_.begun += events->begin.Size();
    stats_.ended += events->end.Size();
}

void PairTracker::Update(const SpatialSet& set, PairEvents* events)
{
    ENGINE_ASSERT(events != nullptr, "PairTracker::Update: events is null");
    events->Clear();
    ++stats_.updates;
    begin_.Clear();
    end_.Clear();
    // The pairs of removed keys end.
    Sort(Span<SpatialKey>(removed_.Data(), removed_.Size()), [](SpatialKey x, SpatialKey y) { return x < y; });
    for (usize i = 0; i < removed_.Size(); ++i)
    {
        if (i > 0 && removed_[i] == removed_[i - 1])
        {
            continue;
        }
        ++stats_.removed;
        for (usize k = FirstOf(adjacent_, removed_[i]); k < adjacent_.Size() && adjacent_[k].a == removed_[i]; ++k)
        {
            end_.Push(adjacent_[k]);
            end_.Push(KeyPair{adjacent_[k].b, adjacent_[k].a});
        }
    }
    Sort(Span<SetHandle>(touched_.Data(), touched_.Size()), [](SetHandle x, SetHandle y) { return x < y; });
    usize unique = 0;
    for (usize i = 0; i < touched_.Size(); ++i)
    {
        if (unique == 0 || touched_[unique - 1] != touched_[i])
        {
            touched_[unique++] = touched_[i];
        }
    }
    touched_.Resize(unique);
    Gather(set, touched_, true);
    touched_.Clear();
    removed_.Clear();
    Finish(events);
}

void PairTracker::Recompute(const SpatialSet& set, PairEvents* events)
{
    ENGINE_ASSERT(events != nullptr, "PairTracker::Recompute: events is null");
    events->Clear();
    ++stats_.updates;
    // Every pair is gathered as new against an empty list, and the difference from the old list is what is reported.
    Vector<KeyPair> old;
    for (usize i = 0; i < adjacent_.Size(); ++i)
    {
        old.Push(adjacent_[i]);
    }
    Vector<SetHandle> all;
    set.Traverse([](const Aabbd&) { return NodeVisit::Descend; },
                 [&](SetHandle h, const Item&)
                 {
                     all.Push(h);
                     return true;
                 });
    adjacent_.Clear();
    removed_.Clear();
    begin_.Clear();
    end_.Clear();
    Gather(set, all, false);
    SortUnique(&begin_);
    // begin_ is the new list; end_ (empty: nothing was held) is not used.
    end_.Clear();
    for (usize i = 0; i < old.Size(); ++i)
    {
        end_.Push(old[i]);
    }
    // Report old minus new as ended and new minus old as begun, then install the new list.
    Vector<KeyPair> fresh;
    for (usize i = 0; i < begin_.Size(); ++i)
    {
        fresh.Push(begin_[i]);
    }
    CancelCommon(&begin_, &end_);
    for (usize k = 0; k < begin_.Size(); ++k)
    {
        if (begin_[k].a < begin_[k].b)
        {
            events->begin.Push(begin_[k]);
        }
    }
    for (usize k = 0; k < end_.Size(); ++k)
    {
        if (end_[k].a < end_[k].b)
        {
            events->end.Push(end_[k]);
        }
    }
    stats_.begun += events->begin.Size();
    stats_.ended += events->end.Size();
    adjacent_.Resize(0);
    for (usize i = 0; i < fresh.Size(); ++i)
    {
        adjacent_.Push(fresh[i]);
    }
    touched_.Clear();
    removed_.Clear();
}

}  // namespace vse::spatial
