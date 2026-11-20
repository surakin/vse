#include "spatial/hash_grid.h"

#include "lang/scalar.h"
#include "log/assert.h"

namespace vse::spatial
{

namespace
{

// Cell coordinates fit in 21 bits each (the key packs three): [-2^20, 2^20 - 1]. An item outside is kept in the
// oversized list.
constexpr i32 kCellLimit = 1 << 20;
constexpr u32 kGridSlotMask = 0x00FFFFFFu;

bool GridFinite(const Aabb& b)
{
    return IsFinite(b.min.x) && IsFinite(b.min.y) && IsFinite(b.min.z) && IsFinite(b.max.x) && IsFinite(b.max.y) &&
           IsFinite(b.max.z);
}

}  // namespace

HashGrid::HashGrid(f32 cell_size) : cell_size_(cell_size), inverse_(1.0f), cell_index_(HeapAllocator::Default())
{
    ENGINE_ASSERT(cell_size > 0.0f && IsFinite(cell_size), "HashGrid: the cell size must be above 0 and finite");
    inverse_ = 1.0f / cell_size;
}

i32 HashGrid::CellOf(f32 v) const
{
    // Clamped before the conversion, so a coordinate far outside (or infinite) never overflows; such a cell is beyond
    // every stored item's range.
    f32 c = Floor(v * inverse_);
    if (!(c >= static_cast<f32>(-kCellLimit - 1)))
    {
        c = static_cast<f32>(-kCellLimit - 1);  // also a NaN
    }
    if (c > static_cast<f32>(kCellLimit))
    {
        c = static_cast<f32>(kCellLimit);
    }
    return static_cast<i32>(c);
}

u64 HashGrid::KeyOf(i32 x, i32 y, i32 z)
{
    const u64 mask = (1ull << 21) - 1;
    return (static_cast<u64>(x + kCellLimit) & mask) | ((static_cast<u64>(y + kCellLimit) & mask) << 21) |
           ((static_cast<u64>(z + kCellLimit) & mask) << 42);
}

bool HashGrid::RangeOf(const Aabb& box, Array<i32, 3>* lo, Array<i32, 3>* hi) const
{
    u64 cells = 1;
    for (u32 k = 0; k < 3; ++k)
    {
        (*lo)[k] = CellOf(box.min[k]);
        (*hi)[k] = CellOf(box.max[k]);
        if ((*lo)[k] < -kCellLimit || (*hi)[k] >= kCellLimit)
        {
            return false;  // beyond the coordinates a key holds
        }
        cells *= static_cast<u64>((*hi)[k] - (*lo)[k] + 1);
    }
    return cells <= kMaxCellsPerItem;
}

const HashGrid::Cell* HashGrid::FindCell(i32 x, i32 y, i32 z) const
{
    const u32* index = cell_index_.Find(KeyOf(x, y, z));
    return index != nullptr ? &cells_[*index] : nullptr;
}

void HashGrid::Register(u32 slot)
{
    Slot& s = slots_[slot];
    if (!RangeOf(s.item.box, &s.lo, &s.hi))
    {
        s.state |= kOversizedBit;
        oversized_.Push(slot);
        return;
    }
    s.state &= ~kOversizedBit;
    for (i32 z = s.lo[2]; z <= s.hi[2]; ++z)
    {
        for (i32 y = s.lo[1]; y <= s.hi[1]; ++y)
        {
            for (i32 x = s.lo[0]; x <= s.hi[0]; ++x)
            {
                const u64 key = KeyOf(x, y, z);
                const u32* found = cell_index_.Find(key);
                u32 index;
                if (found != nullptr)
                {
                    index = *found;
                }
                else
                {
                    index = static_cast<u32>(cells_.Size());
                    Cell cell;
                    cell.at = {{x, y, z}};
                    cells_.Push(static_cast<Cell&&>(cell));
                    cell_index_.Insert(key, index);
                }
                cells_[index].slots.Push(slot);
            }
        }
    }
}

void HashGrid::Unregister(u32 slot)
{
    Slot& s = slots_[slot];
    if (s.Oversized())
    {
        for (usize i = 0; i < oversized_.Size(); ++i)
        {
            if (oversized_[i] == slot)
            {
                oversized_[i] = oversized_[oversized_.Size() - 1];
                oversized_.Pop();
                break;
            }
        }
        s.state &= ~kOversizedBit;
        return;
    }
    for (i32 z = s.lo[2]; z <= s.hi[2]; ++z)
    {
        for (i32 y = s.lo[1]; y <= s.hi[1]; ++y)
        {
            for (i32 x = s.lo[0]; x <= s.hi[0]; ++x)
            {
                const u32* found = cell_index_.Find(KeyOf(x, y, z));
                ENGINE_ASSERT(found != nullptr, "HashGrid: an item's cell is missing");
                Vector<u32>& in_cell = cells_[*found].slots;
                for (usize i = 0; i < in_cell.Size(); ++i)
                {
                    if (in_cell[i] == slot)
                    {
                        in_cell[i] = in_cell[in_cell.Size() - 1];
                        in_cell.Pop();
                        break;
                    }
                }
            }
        }
    }
}

i32 HashGrid::SlotOf(ProxyId proxy) const
{
    if (proxy == kInvalidProxy)
    {
        return -1;
    }
    const u32 slot = proxy & kGridSlotMask;
    if (slot >= slots_.Size())
    {
        return -1;
    }
    const Slot& s = slots_[slot];
    if (!s.Live() || s.Generation() != (proxy >> 24))
    {
        return -1;
    }
    return static_cast<i32>(slot);
}

ProxyId HashGrid::Insert(const Aabb& box, SpatialKey key, u32 layer)
{
    ENGINE_ASSERT(!box.IsEmpty() && GridFinite(box), "HashGrid::Insert: the box is empty or not finite");
    if (count_ >= kMaxDynamicProxies)
    {
        return kInvalidProxy;
    }
    u32 slot;
    if (free_slot_ != kNull)
    {
        slot = free_slot_;
        free_slot_ = slots_[slot].next_free;
    }
    else
    {
        slot = static_cast<u32>(slots_.Size());
        slots_.Push(Slot{});
    }
    Slot& s = slots_[slot];
    s.item.box = box;
    s.item.key = key;
    s.item.layer = layer;
    s.state = (s.state & 0xFFu) | kLiveBit;
    s.next_free = kNull;
    Register(slot);
    ++count_;
    return IdOf(slot);
}

bool HashGrid::Remove(ProxyId proxy)
{
    const i32 found = SlotOf(proxy);
    if (found < 0)
    {
        ++stale_;
        return false;
    }
    const u32 slot = static_cast<u32>(found);
    Unregister(slot);
    Slot& s = slots_[slot];
    s.state = (s.Generation() + 1) & 0xFFu;
    s.next_free = free_slot_;
    free_slot_ = slot;
    --count_;
    return true;
}

bool HashGrid::Move(ProxyId proxy, const Aabb& box)
{
    const i32 found = SlotOf(proxy);
    if (found < 0)
    {
        ++stale_;
        return false;
    }
    ENGINE_ASSERT(!box.IsEmpty() && GridFinite(box), "HashGrid::Move: the box is empty or not finite");
    ++moves_;
    const u32 slot = static_cast<u32>(found);
    Slot& s = slots_[slot];
    Array<i32, 3> lo;
    Array<i32, 3> hi;
    const bool fits = RangeOf(box, &lo, &hi);
    if (fits && !s.Oversized() && lo[0] == s.lo[0] && lo[1] == s.lo[1] && lo[2] == s.lo[2] && hi[0] == s.hi[0] &&
        hi[1] == s.hi[1] && hi[2] == s.hi[2])
    {
        s.item.box = box;  // the same cells: only the box changes
        ++moves_same_;
        return false;
    }
    Unregister(slot);
    slots_[slot].item.box = box;
    Register(slot);
    return true;
}

void HashGrid::Clear()
{
    for (usize c = 0; c < cells_.Size(); ++c)
    {
        cells_[c].slots.Clear();
    }
    oversized_.Clear();
    free_slot_ = kNull;
    for (usize i = slots_.Size(); i > 0; --i)
    {
        Slot& s = slots_[i - 1];
        if (s.Live())
        {
            s.state = (s.Generation() + 1) & 0xFFu;
        }
        s.state &= 0xFFu;
        s.next_free = free_slot_;
        free_slot_ = static_cast<u32>(i - 1);
    }
    count_ = 0;
}

bool HashGrid::IsLive(ProxyId proxy) const
{
    return SlotOf(proxy) >= 0;
}

const Item& HashGrid::ItemOf(ProxyId proxy) const
{
    const i32 slot = SlotOf(proxy);
    ENGINE_ASSERT(slot >= 0, "HashGrid::ItemOf: the handle is not live");
    return slots_[static_cast<u32>(slot)].item;
}

HashGridStats HashGrid::Stats() const
{
    HashGridStats s;
    s.items = count_;
    s.cells = static_cast<u32>(cells_.Size());
    for (usize c = 0; c < cells_.Size(); ++c)
    {
        s.occupied_cells += cells_[c].slots.Size() != 0 ? 1u : 0u;
        s.entries += cells_[c].slots.Size();
    }
    s.oversized = static_cast<u32>(oversized_.Size());
    s.moves = moves_;
    s.moves_same_cells = moves_same_;
    s.stale_refused = stale_;
    return s;
}

}  // namespace vse::spatial
