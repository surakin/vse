#pragma once

#include "types.h"

//! Fixed-size array with inline storage, the engine's `std::array`: the size is a
//! compile-time constant and the elements live inside the object. [[Vector]] is
//! every case where the size is only known at runtime or changes.
namespace engine_placeholder
{

/// Array of exactly `N` elements stored inside the object itself.
///
/// An object holding an `Array` is as large as its other members plus `N` elements: there is no
/// pointer and no allocation. It is an aggregate, so it is initialized like a C array
/// (`Array<int, 3> a = {{1, 2, 3}};`) and unmentioned elements are value-initialized. Indexing is not
/// bounds-checked, like [[Vector]].
///
/// - Thread-safety: not-thread-safe. Synchronize externally if shared.
/// - Ownership: owns its elements, which are destroyed with it.
template <typename T, usize N> struct Array
{
    static_assert(N > 0, "Array: a zero-length array has no storage; use Vector or omit it");

    /// The elements; public so the type stays an aggregate.
    T items[N];

    /// Returns a reference to the element at `index`.
    ///
    /// - Param index: element position; must be less than `N` (not checked).
    T& operator[](usize index) { return items[index]; }
    /// Returns a const reference to the element at `index`.
    ///
    /// - Param index: element position; must be less than `N` (not checked).
    const T& operator[](usize index) const { return items[index]; }

    /// Returns a pointer to the first element.
    T* Data() { return items; }
    /// Returns a const pointer to the first element.
    const T* Data() const { return items; }
    /// Returns the number of elements, `N`.
    static constexpr usize Size() { return N; }

    /// Returns a pointer to the first element, for range-for.
    ///
    /// - Internal: the range-for protocol.
    T* begin() { return items; }
    /// Returns a pointer one past the last element, for range-for.
    ///
    /// - Internal: the range-for protocol.
    T* end() { return items + N; }
    /// Returns a const pointer to the first element, for range-for.
    ///
    /// - Internal: the range-for protocol.
    const T* begin() const { return items; }
    /// Returns a const pointer one past the last element, for range-for.
    ///
    /// - Internal: the range-for protocol.
    const T* end() const { return items + N; }
};

}  // namespace engine_placeholder
