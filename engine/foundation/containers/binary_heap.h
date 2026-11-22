#pragma once

#include "containers/vector.h"
#include "lang/utility.h"
#include "log/assert.h"
#include "mem/heap_allocator.h"
#include "types.h"

//! [[BinaryHeap]]: a priority queue in a [[Vector]], for the "pick the best next" loops
//! that were linear scans.
namespace vse
{

/// A binary max-heap of `T` ordered by `Less`: `Top` is an element no other is greater than, `Push` and `Pop` are
/// O(log n).
///
/// Elements that compare equal come out in no promised order, so a deterministic caller makes the order total: put a
/// sequence number in the element and compare it last (`a.priority < b.priority || (a.priority == b.priority &&
/// a.sequence > b.sequence)` pops the oldest of equal priorities first). The heap has no `begin()`/`end()`: its storage
/// order means nothing. `Remove(index)` takes an element out from the middle, at a storage position read with `At`.
///
/// - Thread-safety: not-thread-safe. Synchronize externally if shared.
/// - Ownership: owns its elements, in a [[Vector]] on `Allocator`.
template <typename T, typename Less, typename Allocator = HeapAllocator> class BinaryHeap
{
public:
    /// Creates an empty heap on `allocator`.
    ///
    /// - Param allocator: must outlive the heap.
    /// - Param less: the ordering; `less(a, b)` is true when `a` comes out after `b`.
    explicit BinaryHeap(Allocator& allocator, Less less = Less()) : items_(allocator), less_(less) {}

    /// Creates an empty heap on the shared default allocator.
    ///
    /// - Param less: the ordering.
    explicit BinaryHeap(Less less = Less())
        requires requires { Allocator::Default(); }
        : items_(Allocator::Default()), less_(less)
    {
    }

    /// Adds `value`.
    ///
    /// - Param value: the element, moved in.
    /// - Aborts: growth is needed and the allocator is out of memory.
    void Push(T value)
    {
        items_.Push(Move(value));
        SiftUp(items_.Size() - 1);
    }

    /// Returns the greatest element.
    ///
    /// - Aborts: the heap is empty.
    const T& Top() const
    {
        ENGINE_ASSERT(!items_.Empty(), "BinaryHeap::Top: the heap is empty");
        return items_[0];
    }

    /// Removes the greatest element and returns it.
    ///
    /// - Returns: the element `Top` returned.
    /// - Aborts: the heap is empty.
    T Pop()
    {
        ENGINE_ASSERT(!items_.Empty(), "BinaryHeap::Pop: the heap is empty");
        return Remove(0);
    }

    /// Removes the element at storage position `index` and returns it.
    ///
    /// - Param index: a storage position below `Size()`.
    /// - Returns: the removed element.
    /// - Aborts: `index` is not less than `Size()`.
    T Remove(usize index)
    {
        ENGINE_ASSERT(index < items_.Size(), "BinaryHeap::Remove: index out of range");
        T out = Move(items_[index]);
        const usize last = items_.Size() - 1;
        if (index != last)
        {
            items_[index] = Move(items_[last]);
        }
        items_.Pop();
        if (index < items_.Size())
        {
            SiftDown(SiftUp(index));
        }
        return out;
    }

    /// Returns the element at storage position `index` (for a caller that tracks positions).
    ///
    /// - Param index: a position below `Size()`.
    /// - Aborts: `index` is not less than `Size()`.
    const T& At(usize index) const
    {
        ENGINE_ASSERT(index < items_.Size(), "BinaryHeap::At: index out of range");
        return items_[index];
    }

    /// Returns the number of elements.
    usize Size() const { return items_.Size(); }
    /// Returns true when the heap is empty.
    bool Empty() const { return items_.Empty(); }
    /// Destroys every element and keeps the storage.
    void Clear() { items_.Clear(); }
    /// Makes room for `count` elements.
    ///
    /// - Param count: the number to make room for.
    /// - Aborts: the allocator is out of memory.
    void Reserve(usize count) { items_.Reserve(count); }

private:
    usize SiftUp(usize i)
    {
        while (i > 0)
        {
            const usize parent = (i - 1) / 2;
            if (!less_(items_[parent], items_[i]))
            {
                break;
            }
            Swap(items_[parent], items_[i]);
            i = parent;
        }
        return i;
    }

    void SiftDown(usize i)
    {
        const usize n = items_.Size();
        for (;;)
        {
            usize best = i;
            const usize left = 2 * i + 1;
            const usize right = left + 1;
            if (left < n && less_(items_[best], items_[left]))
            {
                best = left;
            }
            if (right < n && less_(items_[best], items_[right]))
            {
                best = right;
            }
            if (best == i)
            {
                return;
            }
            Swap(items_[i], items_[best]);
            i = best;
        }
    }

    Vector<T, Allocator> items_;
    Less less_;
};

}  // namespace vse
