#pragma once

#include "containers/array.h"
#include "containers/hash.h"
#include "containers/vector.h"
#include "lang/new.h"
#include "lang/utility.h"
#include "log/assert.h"
#include "types.h"

//! The open-addressing table under [[HashMap]] and [[HashSet]] (container audit series, Day 7): linear probing,
//! tombstones, a power-of-two capacity and the walk, written once. A set stores its keys, a map stores
//! `{key, value}` entries; `KeyOf` says where an element's key is.
namespace engine_placeholder::detail
{

// A set's element is its key.
struct SetKeyOf
{
    template <typename K> const K& operator()(const K& k) const { return k; }
};

// A map's element is an entry with a `key` member.
struct MapKeyOf
{
    template <typename E> const auto& operator()(const E& e) const { return e.key; }
};

// The range-for iterator of a table: walks the slots, skipping empty ones and tombstones, and aborts if the table was
// changed while it walks (one integer compare a step; ENGINE_CHECK_SLOW cannot be used in a header).
template <typename Table, typename Element> class OpenTableIterator
{
public:
    OpenTableIterator(Table* table, usize index) : table_(table), index_(index), changes_(table->Changes())
    {
        Settle();
    }

    Element& operator*() const { return table_->At(index_); }
    Element* operator->() const { return &table_->At(index_); }

    OpenTableIterator& operator++()
    {
        ENGINE_ASSERT(table_->Changes() == changes_, "HashMap/HashSet: changed while a range-for walked it");
        ++index_;
        Settle();
        return *this;
    }

    bool operator==(const OpenTableIterator& other) const { return index_ == other.index_; }
    bool operator!=(const OpenTableIterator& other) const { return index_ != other.index_; }

private:
    void Settle()
    {
        while (index_ < table_->SlotCapacity() && !table_->OccupiedAt(index_))
        {
            ++index_;
        }
    }

    Table* table_;
    usize index_;
    u32 changes_;
};

// Open addressing with linear probing over `Element`s, the key of each found by `KeyOf`. The table is a power of two
// in size, made by the first insert; it rehashes when the load, counting tombstones, reaches 0.7: into twice the slots
// if the live elements alone fill at least half of that, otherwise into the same number (which clears the tombstones).
// Elements are constructed in place when inserted and destroyed when erased, rehashed (moved across) or with the
// table. Not thread-safe; the allocator must outlive the table.
template <typename Element, typename KeyOf, typename Allocator> class OpenTable
{
public:
    static constexpr usize kNone = static_cast<usize>(-1);

    explicit OpenTable(Allocator& allocator, usize initial_capacity)
        : allocator_(&allocator), slots_(allocator), first_capacity_(RoundCapacity(initial_capacity))
    {
    }

    ~OpenTable() { DestroyAll(); }

    OpenTable(const OpenTable&) = delete;
    OpenTable& operator=(const OpenTable&) = delete;

    OpenTable(OpenTable&& other) noexcept
        : allocator_(other.allocator_), slots_(Move(other.slots_)), count_(other.count_),
          tombstone_count_(other.tombstone_count_), first_capacity_(other.first_capacity_)
    {
        other.count_ = 0;
        other.tombstone_count_ = 0;
        ++other.changes_;
    }

    OpenTable& operator=(OpenTable&& other) noexcept
    {
        if (this != &other)
        {
            DestroyAll();
            allocator_ = other.allocator_;
            slots_ = Move(other.slots_);
            count_ = other.count_;
            tombstone_count_ = other.tombstone_count_;
            first_capacity_ = other.first_capacity_;
            other.count_ = 0;
            other.tombstone_count_ = 0;
            ++other.changes_;
            ++changes_;
        }
        return *this;
    }

    // The slot of the element whose key equals `key`, or kNone.
    template <typename Q> usize Find(const Q& key) const
    {
        if (slots_.Size() == 0)
        {
            return kNone;  // no table yet, or a moved-from one
        }
        const usize mask = slots_.Size() - 1;
        usize i = Hash<Q>{}(key)&mask;
        for (usize probe = 0; probe < slots_.Size(); ++probe, i = (i + 1) & mask)
        {
            if (slots_[i].state == SlotState::Empty)
            {
                return kNone;  // the probe chain ended: absent
            }
            if (slots_[i].state == SlotState::Occupied && KeyOf{}(At(i)) == key)
            {
                return i;
            }
        }
        return kNone;
    }

    // Whether one more element needs a rehash first.
    bool NeedsGrow() const { return (count_ + tombstone_count_ + 1) * 10 >= slots_.Size() * 7; }

    // Rehashes for one more element: doubles when the live elements need the room, otherwise the same size (which
    // clears the tombstones), and makes the first table of an empty one.
    void Grow()
    {
        const usize cap = slots_.Size();
        if (cap == 0)
        {
            Rehash(first_capacity_);
            return;
        }
        const bool live_needs_room = (count_ + 1) * 20 >= cap * 7;  // live load >= 0.35
        Rehash(live_needs_room ? cap * 2 : cap);
    }

    // The slot to put `key` in: its own slot if present (occupied), else the first tombstone or empty slot of its probe
    // chain. The table has room (call Grow when NeedsGrow says so).
    template <typename Q> usize SlotForInsert(const Q& key) const
    {
        const usize mask = slots_.Size() - 1;
        usize i = Hash<Q>{}(key)&mask;
        usize first_tombstone = slots_.Size();  // the size means "none found yet"
        for (usize probe = 0; probe < slots_.Size(); ++probe, i = (i + 1) & mask)
        {
            if (slots_[i].state == SlotState::Empty)
            {
                return first_tombstone != slots_.Size() ? first_tombstone : i;
            }
            if (slots_[i].state == SlotState::Occupied && KeyOf{}(At(i)) == key)
            {
                return i;
            }
            if (slots_[i].state == SlotState::Tombstone && first_tombstone == slots_.Size())
            {
                first_tombstone = i;
            }
        }
        return first_tombstone != slots_.Size() ? first_tombstone : 0;  // not reached: NeedsGrow keeps an empty slot
    }

    // Makes an element in the free slot `i` (from SlotForInsert) from `args`.
    template <typename... Args> Element& Make(usize i, Args&&... args)
    {
        if (slots_[i].state == SlotState::Tombstone)
        {
            --tombstone_count_;
        }
        ConstructAt<Element>(slots_[i].room.bytes.Data(), Forward<Args>(args)...);
        slots_[i].state = SlotState::Occupied;
        ++count_;
        ++changes_;
        return At(i);
    }

    // Destroys the element in slot `i`, leaving a tombstone until the next rehash.
    void Erase(usize i)
    {
        At(i).~Element();
        slots_[i].state = SlotState::Tombstone;
        --count_;
        ++tombstone_count_;
        ++changes_;
    }

    // Destroys every element and keeps the table.
    void Clear()
    {
        DestroyAll();
        ++changes_;
    }

    // Makes room for `count` live elements without a rehash.
    void Reserve(usize count)
    {
        // Growth starts when (live + tombstones + 1) * 10 >= slots * 7: the table must keep that false at `count`.
        const usize needed = RoundCapacity((count + 1) * 10 / 7 + 1);
        if (needed > slots_.Size())
        {
            Rehash(needed);
        }
    }

    bool OccupiedAt(usize i) const { return slots_[i].state == SlotState::Occupied; }
    Element& At(usize i) { return slots_[i].room.Get(); }
    const Element& At(usize i) const { return slots_[i].room.Get(); }

    template <typename Visit> void ForEach(Visit visit)
    {
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            if (OccupiedAt(i))
            {
                visit(At(i));
            }
        }
    }

    template <typename Visit> void ForEach(Visit visit) const
    {
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            if (OccupiedAt(i))
            {
                visit(At(i));
            }
        }
    }

    OpenTableIterator<OpenTable, Element> begin() { return OpenTableIterator<OpenTable, Element>(this, 0); }
    OpenTableIterator<OpenTable, Element> end() { return OpenTableIterator<OpenTable, Element>(this, slots_.Size()); }
    OpenTableIterator<const OpenTable, const Element> begin() const
    {
        return OpenTableIterator<const OpenTable, const Element>(this, 0);
    }
    OpenTableIterator<const OpenTable, const Element> end() const
    {
        return OpenTableIterator<const OpenTable, const Element>(this, slots_.Size());
    }

    usize Size() const { return count_; }
    usize SlotCapacity() const { return slots_.Size(); }
    usize TombstoneCount() const { return tombstone_count_; }
    u32 Changes() const { return changes_; }

private:
    enum class SlotState : u8
    {
        Empty,
        Occupied,
        Tombstone
    };

    // Raw, aligned room for one element, constructed and destroyed by the table; never read before it is made.
    struct Room
    {
        alignas(Element) Array<byte, sizeof(Element)> bytes;

        Element& Get() { return *Launder(reinterpret_cast<Element*>(bytes.Data())); }
        const Element& Get() const { return *Launder(reinterpret_cast<const Element*>(bytes.Data())); }
    };

    struct Slot
    {
        Slot() {}  // leaves the room uninitialized
        SlotState state = SlotState::Empty;
        Room room;
    };

    // The smallest power of two of at least `n` slots, and at least 8.
    static usize RoundCapacity(usize n)
    {
        usize cap = 8;
        while (cap < n)
        {
            cap *= 2;
        }
        return cap;
    }

    void DestroyAll()
    {
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            if (slots_[i].state == SlotState::Occupied)
            {
                At(i).~Element();
            }
            slots_[i].state = SlotState::Empty;
        }
        count_ = 0;
        tombstone_count_ = 0;
    }

    // Moves every element into a fresh table of `new_capacity` slots (a power of two); the old elements are destroyed
    // after their move. The slot vector is never grown in place, so no live element is relocated bytewise.
    void Rehash(usize new_capacity)
    {
        Vector<Slot, Allocator> old(Move(slots_));
        slots_ = Vector<Slot, Allocator>(*allocator_);
        slots_.Reserve(new_capacity);
        for (usize i = 0; i < new_capacity; ++i)
        {
            slots_.Push(Slot());
        }
        count_ = 0;
        tombstone_count_ = 0;
        ++changes_;
        for (usize i = 0; i < old.Size(); ++i)
        {
            if (old[i].state != SlotState::Occupied)
            {
                continue;
            }
            Element& from = old[i].room.Get();
            const usize index = SlotForInsert(KeyOf{}(from));
            ConstructAt<Element>(slots_[index].room.bytes.Data(), Move(from));
            slots_[index].state = SlotState::Occupied;
            ++count_;
            from.~Element();
            old[i].state = SlotState::Empty;
        }
    }

    Allocator* allocator_ = nullptr;
    Vector<Slot, Allocator> slots_;
    usize count_ = 0;
    usize tombstone_count_ = 0;
    usize first_capacity_ = 16;  // the slots of the first table, a power of two
    u32 changes_ = 0;            // bumped by every insert, erase, clear and rehash; checked by the iterator
};

}  // namespace engine_placeholder::detail
