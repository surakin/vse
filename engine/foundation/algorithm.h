#pragma once

#include "lang/traits.h"

#include "containers/span.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "types.h"

//! Small algorithms over spans and values, so no code needs `<algorithm>`.
//!
//! `Min`, `Max` and `Clamp` take values; `IndexOf` searches a span; `Sort` orders a span in place with a comparison
//! (not stable, no allocation); `StableSort` keeps equal elements in their order (it allocates a scratch buffer from
//! the heap); `LowerBound`, `UpperBound` and `BinarySearch` search a sorted span; `NthElement` and `PartialSort` select
//! the best `k` without sorting the rest (container audit series, Day 3). The sorts also take a [[Vector]] or anything
//! else with `Data()` and `Size()`.

namespace engine_placeholder
{

/// Returns the smaller of two values (the first if they are equal).
template <typename T> constexpr const T& Min(const T& a, const T& b)
{
    return b < a ? b : a;
}

/// Returns the larger of two values (the first if they are equal).
template <typename T> constexpr const T& Max(const T& a, const T& b)
{
    return a < b ? b : a;
}

/// Returns `value` limited to the range from `low` to `high`.
///
/// - Param low: smallest result; must not be greater than `high`.
template <typename T> constexpr T Clamp(T value, T low, T high)
{
    return value < low ? low : (high < value ? high : value);
}

/// Returns the position of the first element equal to `value`, or the size when there is none.
///
/// - Param items: a [[Span]], [[Vector]] or anything else with `Data()` and `Size()`.
template <typename C, typename V> constexpr usize IndexOf(const C& items, const V& value)
{
    for (usize i = 0; i < items.Size(); ++i)
    {
        if (items.Data()[i] == value)
        {
            return i;
        }
    }
    return items.Size();
}

namespace detail_sort
{

/// Sorts `count` elements by insertion (stable); the base case of `Sort` and `StableSort`.
template <typename T, typename Less> void InsertionSort(T* items, usize count, Less less)
{
    for (usize i = 1; i < count; ++i)
    {
        T value = static_cast<T&&>(items[i]);
        usize j = i;
        while (j > 0 && less(value, items[j - 1]))
        {
            items[j] = static_cast<T&&>(items[j - 1]);
            --j;
        }
        items[j] = static_cast<T&&>(value);
    }
}

/// Restores the heap property below `start` in a heap of `count` elements; the step of `Sort`.
template <typename T, typename Less> void SiftDown(T* items, usize start, usize count, Less less)
{
    usize root = start;
    for (;;)
    {
        usize child = root * 2 + 1;
        if (child >= count)
        {
            return;
        }
        if (child + 1 < count && less(items[child], items[child + 1]))
        {
            ++child;
        }
        if (!less(items[root], items[child]))
        {
            return;
        }
        T tmp = static_cast<T&&>(items[root]);
        items[root] = static_cast<T&&>(items[child]);
        items[child] = static_cast<T&&>(tmp);
        root = child;
    }
}

}  // namespace detail_sort

namespace detail_sort
{

/// Swaps two elements by move.
template <typename T> void SwapItems(T& a, T& b)
{
    T tmp = static_cast<T&&>(a);
    a = static_cast<T&&>(b);
    b = static_cast<T&&>(tmp);
}

/// Heap sort of `count` elements: the fallback of `Sort` when quick sort degrades.
template <typename T, typename Less> void HeapSortItems(T* p, usize count, Less less)
{
    for (usize i = count / 2; i-- > 0;)
    {
        SiftDown(p, i, count, less);
    }
    for (usize end = count - 1; end > 0; --end)
    {
        SwapItems(p[0], p[end]);
        SiftDown(p, 0, end, less);
    }
}

/// Quick sort with a median-of-three pivot; recurses into the smaller side, and falls back to heap sort past `depth`
/// levels.
template <typename T, typename Less> void IntroSortItems(T* p, usize n, int depth, Less less)
{
    while (n > 16)
    {
        if (depth-- == 0)
        {
            HeapSortItems(p, n, less);
            return;
        }
        const usize mid = n / 2;
        if (less(p[mid], p[0]))
        {
            SwapItems(p[mid], p[0]);
        }
        if (less(p[n - 1], p[0]))
        {
            SwapItems(p[n - 1], p[0]);
        }
        if (less(p[n - 1], p[mid]))
        {
            SwapItems(p[n - 1], p[mid]);
        }
        SwapItems(p[mid], p[n - 1]);  // the median is the pivot, parked at the end
        usize store = 0;
        for (usize j = 0; j + 1 < n; ++j)
        {
            if (less(p[j], p[n - 1]))
            {
                SwapItems(p[store++], p[j]);
            }
        }
        SwapItems(p[store], p[n - 1]);
        const usize left = store, right = n - store - 1;
        if (left < right)
        {
            IntroSortItems(p, left, depth, less);
            p += store + 1;
            n = right;
        }
        else
        {
            IntroSortItems(p + store + 1, right, depth, less);
            n = left;
        }
    }
    InsertionSort(p, n, less);
}

}  // namespace detail_sort

/// Sorts the span in place so that `less(b, a)` is false for every earlier `a` and later `b`. Not stable. Introsort:
/// quick sort with a median-of-three pivot, heap sort when it degrades, insertion sort for small runs; O(n log n) worst
/// case, no allocation.
///
/// - Param less: a strict weak ordering, `bool less(const T&, const T&)`.
template <typename T, typename Less> void Sort(Span<T> items, Less less)
{
    const usize n = items.Size();
    int depth = 0;
    for (usize m = n; m > 1; m >>= 1)
    {
        depth += 2;
    }
    detail_sort::IntroSortItems(items.Data(), n, depth, less);
}

/// Sorts the span in place with `operator<`.
template <typename T> void Sort(Span<T> items)
{
    Sort(items, [](const T& a, const T& b) { return a < b; });
}

/// Sorts like `Sort`, but elements that compare equal keep their order. Any movable `T` (container audit Day 3; it
/// was trivially copyable types only): the scratch buffer is a [[Vector]].
///
/// - Param less: a strict weak ordering.
/// - Aborts: the scratch buffer cannot be allocated.
template <typename T, typename Less> void StableSort(Span<T> items, Less less)
{
    const usize n = items.Size();
    // Bottom-up merge sort with insertion-sorted runs of 8.
    constexpr usize kRun = 8;
    T* p = items.Data();
    for (usize i = 0; i < n; i += kRun)
    {
        detail_sort::InsertionSort(p + i, (n - i < kRun) ? n - i : kRun, less);
    }
    if (n <= kRun)
    {
        return;
    }
    Vector<T> scratch;
    scratch.Reserve(n);
    for (usize width = kRun; width < n; width *= 2)
    {
        scratch.Clear();
        for (usize lo = 0; lo < n; lo += 2 * width)
        {
            const usize mid = (lo + width < n) ? lo + width : n;
            const usize hi = (lo + 2 * width < n) ? lo + 2 * width : n;
            usize a = lo, b = mid;
            while (a < mid && b < hi)
            {
                scratch.Push(static_cast<T&&>(less(p[b], p[a]) ? p[b++] : p[a++]));
            }
            while (a < mid)
            {
                scratch.Push(static_cast<T&&>(p[a++]));
            }
            while (b < hi)
            {
                scratch.Push(static_cast<T&&>(p[b++]));
            }
        }
        for (usize i = 0; i < n; ++i)
        {
            p[i] = static_cast<T&&>(scratch[i]);
        }
    }
}

/// Sorts a [[Vector]] (or anything with mutable `Data()` and `Size()`) in place, as `Sort` on its span.
///
/// - Param items: the container.
/// - Param less: a strict weak ordering.
template <typename C, typename Less>
    requires requires(C& c) {
        c.Data();
        c.Size();
    } && (!IsSame<RemoveCVRef<C>, Span<RemoveReference<decltype(*static_cast<C*>(nullptr)->Data())>>>)
void Sort(C& items, Less less)
{
    Sort(Span<RemoveReference<decltype(*items.Data())>>(items.Data(), items.Size()), less);
}

/// Sorts a [[Vector]] (or anything with mutable `Data()` and `Size()`) in place, keeping equal elements in order.
///
/// - Param items: the container.
/// - Param less: a strict weak ordering.
/// - Aborts: the scratch buffer cannot be allocated.
template <typename C, typename Less>
    requires requires(C& c) {
        c.Data();
        c.Size();
    } && (!IsSame<RemoveCVRef<C>, Span<RemoveReference<decltype(*static_cast<C*>(nullptr)->Data())>>>)
void StableSort(C& items, Less less)
{
    StableSort(Span<RemoveReference<decltype(*items.Data())>>(items.Data(), items.Size()), less);
}

/// Returns the position of the first element of the sorted span that is not before `value`: `less(items[i], value)`
/// is false there and true before it. Returns the size when every element is before `value`.
///
/// - Param items: sorted by `less`.
/// - Param value: what to search for; compared as `less(element, value)`.
/// - Param less: the ordering the span is sorted by.
template <typename T, typename V, typename Less> usize LowerBound(Span<T> items, const V& value, Less less)
{
    usize lo = 0;
    usize hi = items.Size();
    while (lo < hi)
    {
        const usize mid = lo + (hi - lo) / 2;
        if (less(items.Data()[mid], value))
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

/// Returns the position of the first element of the sorted span that comes after `value`: `less(value, items[i])` is
/// true there and false before it. Returns the size when none does.
///
/// - Param items: sorted by `less`.
/// - Param value: what to search for; compared as `less(value, element)`.
/// - Param less: the ordering the span is sorted by.
template <typename T, typename V, typename Less> usize UpperBound(Span<T> items, const V& value, Less less)
{
    usize lo = 0;
    usize hi = items.Size();
    while (lo < hi)
    {
        const usize mid = lo + (hi - lo) / 2;
        if (!less(value, items.Data()[mid]))
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

/// Returns the position of an element equal to `value` in the sorted span (neither before nor after it), or the size
/// when there is none.
///
/// - Param items: sorted by `less`.
/// - Param value: what to search for.
/// - Param less: the ordering, used both ways (`less(element, value)` and `less(value, element)`).
template <typename T, typename V, typename Less> usize BinarySearch(Span<T> items, const V& value, Less less)
{
    const usize at = LowerBound(items, value, less);
    return at < items.Size() && !less(value, items.Data()[at]) ? at : items.Size();
}

/// `LowerBound` over a sorted [[Vector]] (or anything with `Data()` and `Size()`).
///
/// - Param items: sorted by `less`.
/// - Param value: what to search for.
/// - Param less: the ordering.
template <typename C, typename V, typename Less>
    requires requires(const C& c) {
        c.Data();
        c.Size();
    } && (!IsSame<RemoveCVRef<C>, Span<RemoveReference<decltype(*static_cast<const C*>(nullptr)->Data())>>>)
usize LowerBound(const C& items, const V& value, Less less)
{
    return LowerBound(Span<RemoveReference<decltype(*items.Data())>>(items.Data(), items.Size()), value, less);
}

/// `UpperBound` over a sorted [[Vector]] (or anything with `Data()` and `Size()`).
///
/// - Param items: sorted by `less`.
/// - Param value: what to search for.
/// - Param less: the ordering.
template <typename C, typename V, typename Less>
    requires requires(const C& c) {
        c.Data();
        c.Size();
    } && (!IsSame<RemoveCVRef<C>, Span<RemoveReference<decltype(*static_cast<const C*>(nullptr)->Data())>>>)
usize UpperBound(const C& items, const V& value, Less less)
{
    return UpperBound(Span<RemoveReference<decltype(*items.Data())>>(items.Data(), items.Size()), value, less);
}

/// `BinarySearch` over a sorted [[Vector]] (or anything with `Data()` and `Size()`).
///
/// - Param items: sorted by `less`.
/// - Param value: what to search for.
/// - Param less: the ordering.
template <typename C, typename V, typename Less>
    requires requires(const C& c) {
        c.Data();
        c.Size();
    } && (!IsSame<RemoveCVRef<C>, Span<RemoveReference<decltype(*static_cast<const C*>(nullptr)->Data())>>>)
usize BinarySearch(const C& items, const V& value, Less less)
{
    return BinarySearch(Span<RemoveReference<decltype(*items.Data())>>(items.Data(), items.Size()), value, less);
}

/// Reorders the span so that the element at `nth` is the one a full sort would put there, every element before it is
/// not after it and every element after it is not before it (introselect: quick select with a median-of-three pivot,
/// heap select past a depth limit; O(n) on average, no allocation). Not stable.
///
/// - Param nth: the position to settle; a value at or past the size does nothing.
/// - Param less: a strict weak ordering.
template <typename T, typename Less> void NthElement(Span<T> items, usize nth, Less less)
{
    T* p = items.Data();
    usize lo = 0;
    usize hi = items.Size();
    if (nth >= hi)
    {
        return;
    }
    int depth = 0;
    for (usize m = hi; m > 1; m >>= 1)
    {
        depth += 2;
    }
    while (hi - lo > 16)
    {
        if (depth-- == 0)
        {
            // Degraded: sort the rest of the range, which settles `nth` too (n log n, rare).
            detail_sort::HeapSortItems(p + lo, hi - lo, less);
            return;
        }
        const usize n = hi - lo;
        T* q = p + lo;
        const usize mid = n / 2;
        if (less(q[mid], q[0]))
        {
            detail_sort::SwapItems(q[mid], q[0]);
        }
        if (less(q[n - 1], q[0]))
        {
            detail_sort::SwapItems(q[n - 1], q[0]);
        }
        if (less(q[n - 1], q[mid]))
        {
            detail_sort::SwapItems(q[n - 1], q[mid]);
        }
        detail_sort::SwapItems(q[mid], q[n - 1]);
        usize store = 0;
        for (usize j = 0; j + 1 < n; ++j)
        {
            if (less(q[j], q[n - 1]))
            {
                detail_sort::SwapItems(q[store++], q[j]);
            }
        }
        detail_sort::SwapItems(q[store], q[n - 1]);
        const usize pivot = lo + store;
        if (pivot == nth)
        {
            return;
        }
        if (nth < pivot)
        {
            hi = pivot;
        }
        else
        {
            lo = pivot + 1;
        }
    }
    detail_sort::InsertionSort(p + lo, hi - lo, less);
}

/// Sorts the first `count` elements of the span into the order a full sort would give them, leaving the rest in no
/// particular order: "keep the best `count`". O(n + count log count) on average.
///
/// - Param count: how many to settle; at or past the size, the whole span is sorted.
/// - Param less: a strict weak ordering.
template <typename T, typename Less> void PartialSort(Span<T> items, usize count, Less less)
{
    if (count >= items.Size())
    {
        Sort(items, less);
        return;
    }
    if (count == 0)
    {
        return;
    }
    NthElement(items, count - 1, less);
    Sort(Span<T>(items.Data(), count - 1), less);
}

}  // namespace engine_placeholder
