// The one file that names FreeType (platform_neutral_check, tools/platform_neutral_check/library_boundary.txt).
#include "text/library.h"

#include <atomic>
#include <cstring>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

#include "mem/heap_allocator.h"

namespace engine_placeholder::text {

namespace {

// FreeType frees by pointer alone, so every block carries a header that remembers its size (16 bytes keeps the 16-byte alignment).
constexpr usize kHeader = 16;

struct Counters {
    std::atomic<u64> allocations{0};
    std::atomic<u64> frees{0};
    std::atomic<u64> live{0};
    std::atomic<u64> peak{0};
};

void* AllocBlock(Counters& c, usize size) {
    void* base = HeapAllocator::Default().Alloc(size + kHeader, 16);
    if (base == nullptr) return nullptr;
    *static_cast<usize*>(base) = size;
    c.allocations.fetch_add(1, std::memory_order_relaxed);
    const u64 live = c.live.fetch_add(size, std::memory_order_relaxed) + size;
    u64 peak = c.peak.load(std::memory_order_relaxed);
    while (live > peak && !c.peak.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {}
    return static_cast<byte*>(base) + kHeader;
}

void FreeBlock(Counters& c, void* block) {
    if (block == nullptr) return;
    void* base = static_cast<byte*>(block) - kHeader;
    const usize size = *static_cast<usize*>(base);
    c.frees.fetch_add(1, std::memory_order_relaxed);
    c.live.fetch_sub(size, std::memory_order_relaxed);
    HeapAllocator::Default().Free(base, size + kHeader, 16);
}

void* FtAlloc(FT_Memory memory, long size) {
    return AllocBlock(*static_cast<Counters*>(memory->user), static_cast<usize>(size));
}

void FtFree(FT_Memory memory, void* block) { FreeBlock(*static_cast<Counters*>(memory->user), block); }

void* FtRealloc(FT_Memory memory, long cur_size, long new_size, void* block) {
    Counters& c = *static_cast<Counters*>(memory->user);
    void* fresh = AllocBlock(c, static_cast<usize>(new_size));
    if (fresh == nullptr) return nullptr; // FreeType keeps the old block when a reallocation fails
    if (block != nullptr) {
        const usize keep = static_cast<usize>(cur_size < new_size ? cur_size : new_size);
        std::memcpy(fresh, block, keep);
        FreeBlock(c, block);
    }
    return fresh;
}

} // namespace

bool ProbeLibrary(LibraryVersion* version, LibraryAllocationStats* stats) {
    Counters counters;
    FT_MemoryRec_ memory{};
    memory.user = &counters;
    memory.alloc = &FtAlloc;
    memory.free = &FtFree;
    memory.realloc = &FtRealloc;

    FT_Library library = nullptr;
    if (FT_New_Library(&memory, &library) != 0) return false;
    FT_Add_Default_Modules(library);
    FT_Int major = 0;
    FT_Int minor = 0;
    FT_Int patch = 0;
    FT_Library_Version(library, &major, &minor, &patch);
    const bool done = FT_Done_Library(library) == 0;

    if (version != nullptr) {
        version->major = static_cast<u32>(major);
        version->minor = static_cast<u32>(minor);
        version->patch = static_cast<u32>(patch);
    }
    if (stats != nullptr) {
        stats->allocations = counters.allocations.load();
        stats->frees = counters.frees.load();
        stats->peak_bytes = counters.peak.load();
        stats->live_bytes_at_end = counters.live.load();
    }
    return done;
}

} // namespace engine_placeholder::text
