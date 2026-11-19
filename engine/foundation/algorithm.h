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

/// Orders three elements so that `*a`, `*b`, `*c` are ascending (the median ends in `*b`).
template <typename T, typename Less> void Sort3(T& a, T& b, T& c, Less less)
{
    if (less(b, a))
    {
        SwapItems(a, b);
    }
    if (less(c, b))
    {
        SwapItems(b, c);
        if (less(b, a))
        {
            SwapItems(a, b);
        }
    }
}

/// Insertion sort that gives up after `limit` moves: true when it finished (the range was nearly sorted).
template <typename T, typename Less> bool PartialInsertionSort(T* p, usize n, Less less)
{
    constexpr usize kLimit = 8;
    usize moves = 0;
    for (usize i = 1; i < n; ++i)
    {
        if (!less(p[i], p[i - 1]))
        {
            continue;
        }
        T value = static_cast<T&&>(p[i]);
        usize j = i;
        do
        {
            p[j] = static_cast<T&&>(p[j - 1]);
            --j;
        } while (j > 0 && less(value, p[j - 1]));
        p[j] = static_cast<T&&>(value);
        moves += i - j;
        if (moves > kLimit)
        {
            return false;
        }
    }
    return true;
}

/// Partitions around the pivot in `p[0]`: smaller elements to its left, the rest to its right. Returns the pivot's
/// position, and whether nothing had to move (the range was already partitioned).
template <typename T, typename Less> usize PartitionRight(T* p, usize n, Less less, bool* already)
{
    T pivot = static_cast<T&&>(p[0]);
    usize first = 1;
    usize last = n;
    while (first < last && less(p[first], pivot))
    {
        ++first;
    }
    while (first < last && !less(p[last - 1], pivot))
    {
        --last;
    }
    *already = first >= last;
    while (first < last)
    {
        SwapItems(p[first], p[last - 1]);
        ++first;
        --last;
        while (first < last && less(p[first], pivot))
        {
            ++first;
        }
        while (first < last && !less(p[last - 1], pivot))
        {
            --last;
        }
    }
    const usize at = first - 1;
    p[0] = static_cast<T&&>(p[at]);
    p[at] = static_cast<T&&>(pivot);
    return at;
}

/// Partitions around the pivot in `p[0]` when every element is known to be at least the pivot (it equals the element
/// before the range): the elements equal to it go left. Returns the pivot's position; `p[0..at]` all equal it.
template <typename T, typename Less> usize PartitionLeft(T* p, usize n, Less less)
{
    T pivot = static_cast<T&&>(p[0]);
    usize first = 1;
    usize last = n;
    while (first < last && !less(pivot, p[first]))
    {
        ++first;
    }
    while (first < last && less(pivot, p[last - 1]))
    {
        --last;
    }
    while (first < last)
    {
        SwapItems(p[first], p[last - 1]);
        ++first;
        --last;
        while (first < last && !less(pivot, p[first]))
        {
            ++first;
        }
        while (first < last && less(pivot, p[last - 1]))
        {
            --last;
        }
    }
    const usize at = first - 1;
    p[0] = static_cast<T&&>(p[at]);
    p[at] = static_cast<T&&>(pivot);
    return at;
}

/// Pattern-defeating quicksort (after Orson Peters' pdqsort): median-of-three pivots (a ninther past 128 elements), a
/// partition that moves few elements, equal keys grouped in one pass (a range whose pivot equals the element before
/// it), nearly sorted ranges finished by a bounded insertion sort, and on unbalanced splits a shuffle and finally heap
/// sort, so the worst case stays O(n log n). `leftmost` is false when `p[-1]` exists and is not greater than any
/// element.
template <typename T, typename Less> void PdqSort(T* p, usize n, Less less, int bad_allowed, bool leftmost)
{
    constexpr usize kInsertion = 24;
    constexpr usize kNinther = 128;
    for (;;)
    {
        if (n < kInsertion)
        {
            InsertionSort(p, n, less);
            return;
        }
        const usize half = n / 2;
        if (n > kNinther)
        {
            Sort3(p[0], p[half], p[n - 1], less);
            Sort3(p[1], p[half - 1], p[n - 2], less);
            Sort3(p[2], p[half + 1], p[n - 3], less);
            Sort3(p[half - 1], p[half], p[half + 1], less);
            SwapItems(p[0], p[half]);
        }
        else
        {
            Sort3(p[half], p[0], p[n - 1], less);  // the median of three into p[0]
        }
        if (!leftmost && !less(p[-1], p[0]))
        {
            // The pivot equals the element before the range, so nothing here is smaller: skip every element equal to
            // it.
            const usize at = PartitionLeft(p, n, less);
            p += at + 1;
            n -= at + 1;
            continue;
        }
        bool already = false;
        const usize at = PartitionRight(p, n, less, &already);
        const usize left = at;
        const usize right = n - at - 1;
        if (left < n / 8 || right < n / 8)
        {
            if (--bad_allowed <= 0)
            {
                HeapSortItems(p, n, less);
                return;
            }
            // Unbalanced: shuffle a few elements so the next pivots are not chosen the same way.
            if (left >= kInsertion)
            {
                SwapItems(p[0], p[left / 4]);
                SwapItems(p[left - 1], p[left - left / 4]);
            }
            if (right >= kInsertion)
            {
                SwapItems(p[at + 1], p[at + 1 + right / 4]);
                SwapItems(p[n - 1], p[n - right / 4]);
            }
        }
        else if (already && PartialInsertionSort(p, left, less) && PartialInsertionSort(p + at + 1, right, less))
        {
            return;  // it was nearly sorted
        }
        PdqSort(p, left, less, bad_allowed, leftmost);
        p += at + 1;
        n = right;
        leftmost = false;
    }
}

}  // namespace detail_sort

/// Sorts the span in place so that `less(b, a)` is false for every earlier `a` and later `b`. Not stable.
/// Pattern-defeating quicksort: few moves, equal keys grouped in one pass, sorted and nearly sorted input in about one
/// pass, heap sort when the splits keep coming out unbalanced, so O(n log n) in the worst case (container audit
/// follow-up). In place, no allocation. (Sorting large elements through their positions was measured and lost: the
/// indirection costs a cache miss per comparison.)
///
/// - Param less: a strict weak ordering, `bool less(const T&, const T&)`.
template <typename T, typename Less> void Sort(Span<T> items, Less less)
{
    const usize n = items.Size();
    if (n < 2)
    {
        return;
    }
    int bad = 1;
    for (usize m = n; m > 1; m >>= 1)
    {
        ++bad;
    }
    detail_sort::PdqSort(items.Data(), n, less, bad, true);
}

/// Sorts the span in place with `operator<`.
template <typename T> void Sort(Span<T> items)
{
    Sort(items, [](const T& a, const T& b) { return a < b; });
}

/// Sorts like `Sort`, but elements that compare equal keep their order, using `scratch` as the merge buffer: a caller
/// that sorts every frame keeps one scratch vector and allocates nothing once it has grown (container audit Day 6).
/// An already ordered span costs one pass and no merge.
///
/// - Param less: a strict weak ordering.
/// - Param scratch: emptied and used as the buffer; its contents afterwards are unspecified.
/// - Aborts: the scratch buffer cannot grow.
template <typename T, typename Less, typename A> void StableSort(Span<T> items, Less less, Vector<T, A>& scratch)
{
    const usize n = items.Size();
    T* p = items.Data();
    bool ordered = true;
    for (usize i = 1; i < n && ordered; ++i)
    {
        ordered = !less(p[i], p[i - 1]);
    }
    if (ordered)
    {
        return;
    }
    // Bottom-up merge sort with insertion-sorted runs of 8.
    constexpr usize kRun = 8;
    for (usize i = 0; i < n; i += kRun)
    {
        detail_sort::InsertionSort(p + i, (n - i < kRun) ? n - i : kRun, less);
    }
    if (n <= kRun)
    {
        return;
    }
    scratch.Clear();
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
    scratch.Clear();
}

/// Sorts like `Sort`, but elements that compare equal keep their order. Any movable `T` (container audit Day 3; it
/// was trivially copyable types only); the merge buffer is made for the call.
///
/// - Param less: a strict weak ordering.
/// - Aborts: the scratch buffer cannot be allocated.
template <typename T, typename Less> void StableSort(Span<T> items, Less less)
{
    Vector<T> scratch;
    StableSort(items, less, scratch);
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
