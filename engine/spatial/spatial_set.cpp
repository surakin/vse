#include "spatial/spatial_set.h"

#include "lang/scalar.h"
#include "log/assert.h"

namespace engine_placeholder::spatial {

namespace {

constexpr u32 kSetSlotMask = 0x00FFFFFFu;

bool SetFinite(const Aabb& b) {
    return IsFinite(b.min.x) && IsFinite(b.min.y) && IsFinite(b.min.z) && IsFinite(b.max.x) && IsFinite(b.max.y) && IsFinite(b.max.z);
}

} // namespace

// The grid is made only with a cell of 1 when the set uses a tree: its cell size is checked only when it is used.
SpatialSet::SpatialSet(const SpatialSetConfig& config)
    : config_(config), dynamic_(config.bvh), grid_(config.dynamic == DynamicKind::Grid ? config.grid_cell_size : 1.0f) {}

i32 SpatialSet::SlotOf(SetHandle handle) const {
    if (handle == kInvalidSetHandle) return -1;
    const u32 slot = handle & kSetSlotMask;
    if (slot >= slots_.Size()) return -1;
    const Slot& s = slots_[slot];
    if (!s.Live() || s.Generation() != (handle >> 24)) return -1;
    return static_cast<i32>(slot);
}

u32 SpatialSet::TakeSlot(const Aabb& box, SpatialKey key, u32 layer) {
    u32 slot;
    if (free_slot_ != kNull) {
        slot = free_slot_;
        free_slot_ = slots_[slot].next_free;
    } else {
        slot = static_cast<u32>(slots_.Size());
        slots_.Push(Slot{});
    }
    Slot& s = slots_[slot];
    s.item.box = box;
    s.item.key = key;
    s.item.layer = layer;
    s.proxy = kInvalidProxy;
    s.state = (s.state & 0xFFu) | kLiveBit;
    s.next_free = kNull;
    ++count_;
    return slot;
}

bool SpatialSet::InsertDynamic(u32 slot) {
    Slot& s = slots_[slot];
    // The structure stores the slot as its key; the caller's key and layer stay in the slot (the layer is copied too, so a structure's own queries
    // would see it, though the set filters on its slot's item).
    s.proxy = config_.dynamic == DynamicKind::Bvh ? dynamic_.Insert(s.item.box, slot, s.item.layer) : grid_.Insert(s.item.box, slot, s.item.layer);
    return s.proxy != kInvalidProxy;
}

void SpatialSet::RemoveDynamic(u32 slot) {
    Slot& s = slots_[slot];
    if (config_.dynamic == DynamicKind::Bvh) dynamic_.Remove(s.proxy);
    else grid_.Remove(s.proxy);
    s.proxy = kInvalidProxy;
}

void SpatialSet::RemovePending(u32 slot) {
    for (usize i = 0; i < pending_.Size(); ++i) {
        if (pending_[i] == slot) {
            // Keep the order (the traversal order is part of the contract): shift the rest down.
            for (usize j = i + 1; j < pending_.Size(); ++j) pending_[j - 1] = pending_[j];
            pending_.Pop();
            return;
        }
    }
}

SetHandle SpatialSet::AddStatic(const Aabb& box, SpatialKey key, u32 layer) {
    ENGINE_ASSERT(!box.IsEmpty() && SetFinite(box), "SpatialSet::AddStatic: the box is empty or not finite");
    if (count_ >= kMaxDynamicProxies) return kInvalidSetHandle;
    const u32 slot = TakeSlot(box, key, layer);
    slots_[slot].state |= kStaticBit;
    pending_.Push(slot);
    return HandleOf(slot);
}

SetHandle SpatialSet::AddDynamic(const Aabb& box, SpatialKey key, u32 layer) {
    ENGINE_ASSERT(!box.IsEmpty() && SetFinite(box), "SpatialSet::AddDynamic: the box is empty or not finite");
    if (count_ >= kMaxDynamicProxies) return kInvalidSetHandle;
    const u32 slot = TakeSlot(box, key, layer);
    if (!InsertDynamic(slot)) {
        Slot& s = slots_[slot];
        s.state = (s.Generation() + 1) & 0xFFu;
        s.next_free = free_slot_;
        free_slot_ = slot;
        --count_;
        return kInvalidSetHandle;
    }
    return HandleOf(slot);
}

bool SpatialSet::Remove(SetHandle handle) {
    const i32 found = SlotOf(handle);
    if (found < 0) {
        ++stale_;
        return false;
    }
    const u32 slot = static_cast<u32>(found);
    Slot& s = slots_[slot];
    if (s.Static()) {
        if (!s.InTree()) RemovePending(slot); // an entry in the tree is skipped from now on (the slot is no longer live and in the tree)
    } else {
        RemoveDynamic(slot);
    }
    Slot& freed = slots_[slot];
    freed.state = (freed.Generation() + 1) & 0xFFu;
    freed.next_free = free_slot_;
    free_slot_ = slot;
    --count_;
    return true;
}

bool SpatialSet::Move(SetHandle handle, const Aabb& box) {
    const i32 found = SlotOf(handle);
    if (found < 0) {
        ++stale_;
        return false;
    }
    ENGINE_ASSERT(!box.IsEmpty() && SetFinite(box), "SpatialSet::Move: the box is empty or not finite");
    Slot& s = slots_[static_cast<u32>(found)];
    if (s.Static()) {
        ++static_moves_refused_;
        return false;
    }
    s.item.box = box;
    return config_.dynamic == DynamicKind::Bvh ? dynamic_.Move(s.proxy, box) : grid_.Move(s.proxy, box);
}

bool SpatialSet::MakeDynamic(SetHandle handle) {
    const i32 found = SlotOf(handle);
    if (found < 0) {
        ++stale_;
        return false;
    }
    const u32 slot = static_cast<u32>(found);
    if (!slots_[slot].Static()) return true;
    if (!slots_[slot].InTree()) RemovePending(slot);
    slots_[slot].state &= ~(kStaticBit | kInTreeBit); // a tree entry is skipped from now on
    if (!InsertDynamic(slot)) {
        // The dynamic structure is full: the item stays static, back in the pending list.
        slots_[slot].state |= kStaticBit;
        pending_.Push(slot);
        return false;
    }
    return true;
}

bool SpatialSet::MakeStatic(SetHandle handle) {
    const i32 found = SlotOf(handle);
    if (found < 0) {
        ++stale_;
        return false;
    }
    const u32 slot = static_cast<u32>(found);
    if (slots_[slot].Static()) return true;
    RemoveDynamic(slot);
    slots_[slot].state |= kStaticBit;
    pending_.Push(slot);
    return true;
}

void SpatialSet::RebuildStatic() {
    ++static_rebuilds_;
    build_items_.Clear();
    for (usize s = 0; s < slots_.Size(); ++s) {
        Slot& slot = slots_[s];
        if (!slot.Live() || !slot.Static()) {
            slot.state &= ~kInTreeBit;
            continue;
        }
        Item entry = slot.item;
        entry.key = s; // the tree finds slots; the visitor maps them to the caller's items
        build_items_.Push(entry);
        slot.state |= kInTreeBit;
    }
    static_.Build(Span<const Item>(build_items_.Data(), build_items_.Size()));
    pending_.Clear();
}

void SpatialSet::Optimize(u32 budget) {
    if (config_.dynamic == DynamicKind::Bvh) dynamic_.Optimize(budget);
}

void SpatialSet::Clear() {
    static_.Clear();
    dynamic_.Clear();
    grid_.Clear();
    pending_.Clear();
    free_slot_ = kNull;
    for (usize i = slots_.Size(); i > 0; --i) {
        Slot& s = slots_[i - 1];
        if (s.Live()) s.state = (s.Generation() + 1) & 0xFFu;
        s.state &= 0xFFu;
        s.proxy = kInvalidProxy;
        s.next_free = free_slot_;
        free_slot_ = static_cast<u32>(i - 1);
    }
    count_ = 0;
}

bool SpatialSet::IsLive(SetHandle handle) const { return SlotOf(handle) >= 0; }

bool SpatialSet::IsStatic(SetHandle handle) const {
    const i32 slot = SlotOf(handle);
    ENGINE_ASSERT(slot >= 0, "SpatialSet::IsStatic: the handle is not live");
    return slots_[static_cast<u32>(slot)].Static();
}

const Item& SpatialSet::ItemOf(SetHandle handle) const {
    const i32 slot = SlotOf(handle);
    ENGINE_ASSERT(slot >= 0, "SpatialSet::ItemOf: the handle is not live");
    return slots_[static_cast<u32>(slot)].item;
}

SpatialSetStats SpatialSet::Stats() const {
    SpatialSetStats s;
    for (usize i = 0; i < slots_.Size(); ++i) {
        const Slot& slot = slots_[i];
        if (!slot.Live()) continue;
        if (slot.Static()) ++s.static_items;
        else ++s.dynamic_items;
    }
    s.pending_static = static_cast<u32>(pending_.Size());
    u32 live_in_tree = 0;
    for (usize i = 0; i < slots_.Size(); ++i) live_in_tree += slots_[i].Live() && slots_[i].InTree() ? 1u : 0u;
    s.stale_tree_entries = static_.ItemCount() - live_in_tree;
    s.static_rebuilds = static_rebuilds_;
    s.static_moves_refused = static_moves_refused_;
    s.stale_refused = stale_;
    return s;
}

} // namespace engine_placeholder::spatial
