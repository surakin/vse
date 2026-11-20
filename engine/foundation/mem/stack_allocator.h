#pragma once

#include "types.h"

//! Stack allocator: LIFO allocation over one reserved block.
//!
//! A marker records an earlier offset; popping to it discards everything
//! allocated since.
namespace vse
{

/// Bump allocator whose allocations can be discarded in LIFO order by
/// rewinding to a saved marker.
///
/// Same storage strategy as [[ArenaAllocator]]; the difference is the marker
/// API. Memory is not zeroed and no destructors are run.
///
/// - Thread-safety: not-thread-safe. Synchronize externally if shared.
/// - Ownership: owns the backing block, released by the destructor. Pointers
///   returned by `Push` are invalidated by popping to an earlier marker.
class StackAllocator
{
public:
    /// Opaque position in the stack: the byte offset at the time it was taken.
    using Marker = usize;

    /// Reserves a block of `capacity` bytes from the OS.
    ///
    /// - Param capacity: size of the backing block in bytes.
    /// - Fails: if the OS refuses the reservation the allocator has zero
    ///   capacity and every `Push` returns nullptr.
    explicit StackAllocator(usize capacity);
    /// Releases the backing block.
    ~StackAllocator();

    StackAllocator(const StackAllocator&) = delete;
    StackAllocator& operator=(const StackAllocator&) = delete;

    /// Bump-allocates `size` bytes aligned to `align`.
    ///
    /// - Param size: number of bytes requested; 0 is allowed.
    /// - Param align: required alignment in bytes; must be a power of two
    ///   (not checked).
    /// - Returns: the start of the new region, or nullptr when fewer than
    ///   `size` bytes remain after aligning.
    /// - Fails: reports exhaustion by returning nullptr; the stack is unchanged.
    void* Push(usize size, usize align);

    /// Returns a marker for the current top of the stack.
    Marker CurrentMarker() const;

    /// Rewinds to `marker`, discarding everything pushed since it was taken.
    ///
    /// - Param marker: a value from an earlier `CurrentMarker` call on this
    ///   allocator that has not already been popped past (not checked).
    void PopToMarker(Marker marker);

private:
    u8* base_ = nullptr;
    usize capacity_ = 0;
    usize offset_ = 0;
};

}  // namespace vse
