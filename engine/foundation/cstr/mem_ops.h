#pragma once

#include <cstdlib>
#include <cstring>

#include "types.h"

//! Raw memory operations (Phase 16): the engine's spelling of the C library's `<cstring>` memory functions and the C heap of
//! `<cstdlib>`. Every function is an inline forwarder, so the compiler still turns a constant-size [[MemCopy]] into moves.
namespace engine_placeholder {

/// Copies `count` bytes from `source` to `destination`; the ranges must not overlap (`memcpy`).
///
/// Param count: zero is allowed with any pointers, including null.
/// Returns: `destination`.
/// Thread-safety: thread-compatible. The ranges must not be written by another thread meanwhile.
inline void* MemCopy(void* destination, const void* source, usize count) noexcept {
    if (count == 0) return destination;
    return std::memcpy(destination, source, count);
}

/// Copies `count` bytes from `source` to `destination`; the ranges may overlap (`memmove`).
///
/// Returns: `destination`.
/// Thread-safety: thread-compatible. The ranges must not be written by another thread meanwhile.
inline void* MemMove(void* destination, const void* source, usize count) noexcept {
    if (count == 0) return destination;
    return std::memmove(destination, source, count);
}

/// Sets `count` bytes at `destination` to `value` (`memset`).
///
/// Returns: `destination`.
/// Thread-safety: thread-compatible. The range must not be accessed by another thread meanwhile.
inline void* MemSet(void* destination, int value, usize count) noexcept {
    if (count == 0) return destination;
    return std::memset(destination, value, count);
}

/// Compares `count` bytes as unsigned values (`memcmp`).
///
/// Returns: negative, zero or positive as the first differing byte of `a` is less than, equal to or greater than that of `b`.
/// Thread-safety: thread-compatible. The ranges must not be written by another thread meanwhile.
inline int MemCompare(const void* a, const void* b, usize count) noexcept {
    if (count == 0) return 0;
    return std::memcmp(a, b, count);
}

/// Finds the first byte equal to `value` in `count` bytes (`memchr`).
///
/// Returns: a pointer to it, or null.
/// Thread-safety: thread-compatible. The range must not be written by another thread meanwhile.
inline const void* MemFind(const void* data, int value, usize count) noexcept {
    if (count == 0) return nullptr;
    return std::memchr(data, value, count);
}

/// Allocates `size` bytes from the C heap, suitably aligned for any scalar (`malloc`).
///
/// Returns: the block, or null when the heap is exhausted. Release it with [[MemFree]].
/// Thread-safety: thread-safe.
inline void* MemAllocate(usize size) noexcept {
    return std::malloc(size);
}

/// Allocates `count` zeroed elements of `size` bytes from the C heap (`calloc`).
///
/// Thread-safety: thread-safe.
inline void* MemAllocateZeroed(usize count, usize size) noexcept {
    return std::calloc(count, size);
}

/// Resizes a block from [[MemAllocate]], keeping its contents up to the smaller size (`realloc`).
///
/// Returns: the new block (the old one is released), or null with the old block untouched.
/// Thread-safety: thread-safe.
inline void* MemReallocate(void* block, usize size) noexcept {
    return std::realloc(block, size);
}

/// Releases a block from [[MemAllocate]] (`free`); null is allowed.
///
/// Thread-safety: thread-safe.
inline void MemFree(void* block) noexcept {
    std::free(block);
}

} // namespace engine_placeholder
