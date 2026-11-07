#pragma once

#include <type_traits>

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
/// Param low: smallest result; must not be greater than `high`.
template <typename T>
constexpr T Clamp(T value, T low, T high) {
    return value < low ? low : (high < value ? high : value);
}

/// Returns the position of the first element equal to `value`, or the size when there is none.
///
/// Param items: a [[Span]], [[Vector]] or anything else with `Data()` and `Size()`.
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

/// Sorts the span in place so that `less(b, a)` is false for every earlier `a` and later `b`. Not stable. Heap sort above 16 elements: O(n log n) worst case, no allocation, no recursion.
///
/// Param less: a strict weak ordering, `bool less(const T&, const T&)`.
template <typename T, typename Less>
void Sort(Span<T> items, Less less) {
    const usize n = items.Size();
    if (n <= 16) {
        detail_sort::InsertionSort(items.Data(), n, less);
        return;
    }
    T* p = items.Data();
    for (usize i = n / 2; i-- > 0;) detail_sort::SiftDown(p, i, n, less);
    for (usize end = n - 1; end > 0; --end) {
        T tmp = static_cast<T&&>(p[0]);
        p[0] = static_cast<T&&>(p[end]);
        p[end] = static_cast<T&&>(tmp);
        detail_sort::SiftDown(p, 0, end, less);
    }
}

/// Sorts the span in place with `operator<`.
template <typename T>
void Sort(Span<T> items) {
    Sort(items, [](const T& a, const T& b) { return a < b; });
}

/// Sorts like `Sort`, but elements that compare equal keep their order.
///
/// Fails: the scratch buffer cannot be allocated; the span is then sorted by insertion, which is stable and slow.
template <typename T, typename Less>
void StableSort(Span<T> items, Less less) {
    static_assert(std::is_trivially_copyable_v<T>, "StableSort: elements are moved through raw scratch memory");
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
