#pragma once

#include "types.h"

//! Linear bump allocator backed by a single mmap'd block. Individual
//! allocations are never freed; Reset rewinds the bump pointer, FreeAll
//! unmaps the whole block.
namespace engine_placeholder {

/// Linear bump allocator over one fixed-capacity block of virtual memory.
///
/// Allocations are never freed individually; [[ArenaAllocator::Reset]] makes
/// the whole capacity reusable and [[ArenaAllocator::FreeAll]] returns the
/// block to the OS. Memory is not zeroed and no destructors are run.
///
/// Thread-safety: not-thread-safe. Synchronize externally if shared.
/// Ownership: owns the backing block; it is released by `FreeAll` or the
///   destructor. Pointers returned by `Alloc` are invalidated by `Reset`
///   and `FreeAll`.
class ArenaAllocator {
public:
    /// Reserves a block of `capacity` bytes from the OS.
    ///
    /// Param capacity: size of the backing block in bytes.
    /// Fails: if the OS refuses the reservation the arena is left with zero
    ///   capacity and every `Alloc` returns nullptr.
    explicit ArenaAllocator(usize capacity);
    /// Releases the backing block, as if by [[ArenaAllocator::FreeAll]].
    ~ArenaAllocator();

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    /// Bump-allocates `size` bytes aligned to `align`.
    ///
    /// Param size: number of bytes requested; 0 is allowed.
    /// Param align: required alignment in bytes; must be a power of two
    ///   (not checked).
    /// Returns: the start of the new region, or nullptr when fewer than
    ///   `size` bytes remain after aligning.
    /// Fails: reports exhaustion by returning nullptr; the arena is unchanged.
    void* Alloc(usize size, usize align);

    /// Rewinds the bump pointer to the start; the backing block stays mapped.
    ///
    /// Invalidates every pointer previously returned by `Alloc`.
    void Reset();

    /// Unmaps the backing block, leaving the arena with zero capacity.
    ///
    /// Safe to call more than once. Invalidates every pointer previously
    /// returned by `Alloc`.
    void FreeAll();

private:
    u8* base_ = nullptr;
    usize capacity_ = 0;
    usize offset_ = 0;
};

} // namespace engine_placeholder
