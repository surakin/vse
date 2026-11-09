#pragma once

#include "lang/new.h"

#include "types.h"

//! General-purpose allocator over the C++ aligned heap, for containers that own
//! storage no arena owns. Unlike the arena it frees individual blocks.
namespace engine_placeholder {

/// Stateless heap allocator that frees individual blocks.
///
/// Satisfies the allocator contract of [[Vector]] (`Alloc`) and, because it has
/// `Free`, makes [[Vector]] release its storage. Use [[HeapAllocator::Default]]
/// for the shared instance. Blocks are always aligned to at least 16 bytes.
///
/// Thread-safety: thread-safe; the type holds no state and the heap is thread-safe.
/// Ownership: the caller owns each block until it passes it to `Free`.
class HeapAllocator {
public:
    /// Returns the process-wide instance; allocation through it is equivalent to any other instance.
    static HeapAllocator& Default() {
        static HeapAllocator instance;
        return instance;
    }

    /// Allocates `size` bytes aligned to `align`; the memory is not zeroed.
    ///
    /// Param size: bytes to allocate; zero is allowed and returns a valid block.
    /// Param align: power-of-two alignment.
    /// Returns: the block, or nullptr if the heap is exhausted.
    void* Alloc(usize size, usize align) {
        return ::operator new(size == 0 ? 1 : size, AlignVal(Aligned(align)), kNoThrow);
    }

    /// Releases a block returned by `Alloc`.
    ///
    /// Param ptr: block from `Alloc`, or nullptr (ignored).
    /// Param size: the size passed to `Alloc`.
    /// Param align: the alignment passed to `Alloc`.
    void Free(void* ptr, usize size, usize align) {
        (void)size;
        ::operator delete(ptr, AlignVal(Aligned(align)));
    }

private:
    static usize Aligned(usize align) { return align < 16 ? 16 : align; }
};

} // namespace engine_placeholder
