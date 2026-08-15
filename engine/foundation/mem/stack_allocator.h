#pragma once

#include "types.h"

// Bump allocator backed by a single mmap'd block (PLAN.md assumption
// #2), exposing LIFO push/pop-to-marker semantics on top of the same
// bump-pointer approach ArenaAllocator uses. A Marker is just the
// offset at some earlier point; PopToMarker() rewinds to it, discarding
// everything allocated since — the arena-vs-stack distinction is purely
// this marker API, not the underlying storage strategy.
namespace engine_placeholder {

class StackAllocator {
public:
    using Marker = usize;

    explicit StackAllocator(usize capacity);
    ~StackAllocator();

    StackAllocator(const StackAllocator&) = delete;
    StackAllocator& operator=(const StackAllocator&) = delete;

    // Returns nullptr if the allocator doesn't have `size` bytes left
    // after aligning to `align` (which must be a power of two).
    void* Push(usize size, usize align);

    Marker CurrentMarker() const;

    // Rewinds to a marker obtained from an earlier CurrentMarker() call
    // on this same allocator. Everything pushed since is discarded.
    void PopToMarker(Marker marker);

private:
    u8* base_ = nullptr;
    usize capacity_ = 0;
    usize offset_ = 0;
};

} // namespace engine_placeholder
