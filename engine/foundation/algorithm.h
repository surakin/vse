#pragma once

#include "lang/traits.h"

#include "containers/span.h"
#include "mem/heap_allocator.h"
#include "types.h"

//! Small algorithms over spans and values, so no code needs `<algorithm>`.
//!
//! `Min`, `Max` and `Clamp` take values; `IndexOf` searches a span; `Sort` orders a span in place with a comparison (not stable, no allocation); `StableSort` keeps equal elements in their order (it allocates a scratch buffer from the heap).

namespace engine_placeholder {

/// Returns the smaller of two values (the first if they are equal).
template <typename T>
constexpr const T& Min(const T& a, const T& b) {
    return b < a ? b : a;
}

/// Returns the larger of two values (the first if they are equal).
template <typename T>
constexpr const T& Max(const T& a, const T& b) {
    return a < b ? b : a;
}

/// Returns `value` limited to the range from `low` to `high`.
///
/// - Param low: smallest result; must not be greater than `high`.
template <typename T>
constexpr T Clamp(T value, T low, T high) {
    return value < low ? low : (high < value ? high : value);
}

/// Returns the position of the first element equal to `value`, or the size when there is none.
///
/// - Param items: a [[Span]], [[Vector]] or anything else with `Data()` and `Size()`.
template <typename C, typename V>
constexpr usize IndexOf(const C& items, const V& value) {
    for (usize i = 0; i < items.Size(); ++i) {
        if (items.Data()[i] == value) return i;
    }
    return items.Size();
}

namespace detail_sort {

/// Sorts `count` elements by insertion (stable); the base case of `Sort` and `StableSort`.
template <typename T, typename Less>
void InsertionSort(T* items, usize count, Less less) {
    for (usize i = 1; i < count; ++i) {
        T value = static_cast<T&&>(items[i]);
        usize j = i;
        while (j > 0 && less(value, items[j - 1])) {
            items[j] = static_cast<T&&>(items[j - 1]);
            --j;
        }
        items[j] = static_cast<T&&>(value);
    }
}

/// Restores the heap property below `start` in a heap of `count` elements; the step of `Sort`.
template <typename T, typename Less>
void SiftDown(T* items, usize start, usize count, Less less) {
    usize root = start;
    for (;;) {
        usize child = root * 2 + 1;
        if (child >= count) return;
        if (child + 1 < count && less(items[child], items[child + 1])) ++child;
        if (!less(items[root], items[child])) return;
        T tmp = static_cast<T&&>(items[root]);
        items[root] = static_cast<T&&>(items[child]);
        items[child] = static_cast<T&&>(tmp);
        root = child;
    }
}

} // namespace detail_sort

namespace detail_sort {

/// Swaps two elements by move.
template <typename T>
void SwapItems(T& a, T& b) {
    T tmp = static_cast<T&&>(a);
    a = static_cast<T&&>(b);
    b = static_cast<T&&>(tmp);
}

/// Heap sort of `count` elements: the fallback of `Sort` when quick sort degrades.
template <typename T, typename Less>
void HeapSortItems(T* p, usize count, Less less) {
    for (usize i = count / 2; i-- > 0;) SiftDown(p, i, count, less);
    for (usize end = count - 1; end > 0; --end) {
        SwapItems(p[0], p[end]);
        SiftDown(p, 0, end, less);
    }
}

/// Quick sort with a median-of-three pivot; recurses into the smaller side, and falls back to heap sort past `depth` levels.
template <typename T, typename Less>
void IntroSortItems(T* p, usize n, int depth, Less less) {
    while (n > 16) {
        if (depth-- == 0) {
            HeapSortItems(p, n, less);
            return;
        }
        const usize mid = n / 2;
        if (less(p[mid], p[0])) SwapItems(p[mid], p[0]);
        if (less(p[n - 1], p[0])) SwapItems(p[n - 1], p[0]);
        if (less(p[n - 1], p[mid])) SwapItems(p[n - 1], p[mid]);
        SwapItems(p[mid], p[n - 1]); // the median is the pivot, parked at the end
        usize store = 0;
        for (usize j = 0; j + 1 < n; ++j) {
            if (less(p[j], p[n - 1])) SwapItems(p[store++], p[j]);
        }
        SwapItems(p[store], p[n - 1]);
        const usize left = store, right = n - store - 1;
        if (left < right) {
            IntroSortItems(p, left, depth, less);
            p += store + 1;
            n = right;
        } else {
            IntroSortItems(p + store + 1, right, depth, less);
            n = left;
        }
    }
    InsertionSort(p, n, less);
}

} // namespace detail_sort

/// Sorts the span in place so that `less(b, a)` is false for every earlier `a` and later `b`. Not stable. Introsort: quick sort with a
/// median-of-three pivot, heap sort when it degrades, insertion sort for small runs; O(n log n) worst case, no allocation.
///
/// - Param less: a strict weak ordering, `bool less(const T&, const T&)`.
template <typename T, typename Less>
void Sort(Span<T> items, Less less) {
    const usize n = items.Size();
    int depth = 0;
    for (usize m = n; m > 1; m >>= 1) depth += 2;
    detail_sort::IntroSortItems(items.Data(), n, depth, less);
}

/// Sorts the span in place with `operator<`.
template <typename T>
void Sort(Span<T> items) {
    Sort(items, [](const T& a, const T& b) { return a < b; });
}

/// Sorts like `Sort`, but elements that compare equal keep their order.
///
/// - Fails: the scratch buffer cannot be allocated; the span is then sorted by insertion, which is stable and slow.
template <typename T, typename Less>
void StableSort(Span<T> items, Less less) {
    static_assert(IsTriviallyCopyable<T>, "StableSort: elements are moved through raw scratch memory");
    const usize n = items.Size();
    // Bottom-up merge sort with insertion-sorted runs of 8.
    constexpr usize kRun = 8;
    T* p = items.Data();
    for (usize i = 0; i < n; i += kRun) detail_sort::InsertionSort(p + i, (n - i < kRun) ? n - i : kRun, less);
    if (n <= kRun) return;
    T* scratch = static_cast<T*>(HeapAllocator::Default().Alloc(n * sizeof(T), alignof(T)));
    if (scratch == nullptr) {
        detail_sort::InsertionSort(p, n, less);
        return;
    }
    for (usize width = kRun; width < n; width *= 2) {
        for (usize lo = 0; lo < n; lo += 2 * width) {
            const usize mid = (lo + width < n) ? lo + width : n;
            const usize hi = (lo + 2 * width < n) ? lo + 2 * width : n;
            usize a = lo, b = mid, out = lo;
            while (a < mid && b < hi) scratch[out++] = static_cast<T&&>(less(p[b], p[a]) ? p[b++] : p[a++]);
            while (a < mid) scratch[out++] = static_cast<T&&>(p[a++]);
            while (b < hi) scratch[out++] = static_cast<T&&>(p[b++]);
        }
        for (usize i = 0; i < n; ++i) p[i] = static_cast<T&&>(scratch[i]);
    }
    HeapAllocator::Default().Free(scratch, n * sizeof(T), alignof(T));
}

} // namespace engine_placeholder
