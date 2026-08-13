#pragma once

#include "types.h"

// Linear bump allocator backed by a single mmap'd block (PLAN.md
// assumption #2 — hand-rolled rather than malloc-backed). Individual
// allocations are never freed; Reset() rewinds the bump pointer for
// reuse, FreeAll() unmaps the whole block.
namespace engine_placeholder {

class ArenaAllocator {
public:
    explicit ArenaAllocator(usize capacity);
    ~ArenaAllocator();

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    // Returns nullptr if the arena doesn't have `size` bytes left after
    // aligning to `align` (which must be a power of two).
    void* Alloc(usize size, usize align);

    // Rewinds the bump pointer; the backing block stays mapped.
    void Reset();

    // Unmaps the backing block. Safe to call more than once.
    void FreeAll();

private:
    u8* base_ = nullptr;
    usize capacity_ = 0;
    usize offset_ = 0;
};

} // namespace engine_placeholder
