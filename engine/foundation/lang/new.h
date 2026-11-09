#pragma once

#include <new>

#include "types.h"

//! Allocation-function support (Phase 16): the engine's spelling of the parts of `<new>` that code outside the allocators needs.
namespace engine_placeholder {

/// The alignment argument of the aligned `operator new`/`operator delete` overloads (`std::align_val_t`).
using AlignVal = std::align_val_t;
/// The tag type of the non-throwing `operator new` overloads (`std::nothrow_t`).
using NoThrowTag = std::nothrow_t;
/// The value of `NoThrowTag` (`std::nothrow`).
inline constexpr const NoThrowTag& kNoThrow = std::nothrow;
/// The exception type the throwing `operator new` reports with (`std::bad_alloc`); the engine builds without exceptions, only tests that replace the global
/// allocation functions name it.
using BadAlloc = std::bad_alloc;

/// Constructs a `T` in the storage at `where` with `args` (placement new); the storage must be suitably aligned and big enough.
///
/// Thread-safety: thread-compatible. Not safe on the same storage from two threads.
template <class T, class... Args>
T* ConstructAt(void* where, Args&&... args) {
    return ::new (where) T(static_cast<Args&&>(args)...);
}

/// Returns a pointer to the object that lives at `where` after the storage was reused (`std::launder`).
template <class T>
constexpr T* Launder(T* where) noexcept {
    return std::launder(where);
}

/// Allocates `size` bytes aligned to `align` from the global heap, or null (non-throwing, aligned `operator new`).
inline void* AlignedNew(usize size, usize align) noexcept {
    return ::operator new(size, AlignVal(align), kNoThrow);
}

/// Releases storage from [[AlignedNew]] (aligned `operator delete`).
inline void AlignedDelete(void* ptr, usize align) noexcept {
    ::operator delete(ptr, AlignVal(align));
}

} // namespace engine_placeholder
