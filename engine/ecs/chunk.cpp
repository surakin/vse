#include "ecs/chunk.h"

#include "lang/new.h"
#include "log/assert.h"

namespace engine_placeholder::ecs {

ChunkPool::~ChunkPool() {
    ENGINE_ASSERT(stats_.chunks == 0, "ChunkPool destroyed while chunks are still handed out");
    Trim();
}

Chunk* ChunkPool::Acquire(u32 archetype, u32 capacity) {
    Chunk* chunk;
    if (kept_.Size() != 0) {
        chunk = kept_[kept_.Size() - 1];
        kept_.Pop();
        --stats_.pooled;
        ++stats_.reused;
    } else {
        void* block = HeapAllocator::Default().Alloc(kChunkBytes, 64);
        if (block == nullptr) {
            ++stats_.refused_allocations;
            return nullptr;
        }
        chunk = static_cast<Chunk*>(block);
    }
    ConstructAt<Chunk>(chunk);
    chunk->archetype = archetype;
    chunk->capacity = capacity;
    ++stats_.chunks;
    stats_.bytes += kChunkBytes;
    return chunk;
}

void ChunkPool::Release(Chunk* chunk) {
    ENGINE_ASSERT(chunk != nullptr && stats_.chunks != 0, "ChunkPool::Release: not a chunk of this pool");
    --stats_.chunks;
    stats_.bytes -= kChunkBytes;
    if (stats_.pooled < budget_ && kept_.TryReserve(kept_.Size() + 1)) {
        kept_.Push(chunk);
        ++stats_.pooled;
        return;
    }
    HeapAllocator::Default().Free(chunk, kChunkBytes, 64);
}

void ChunkPool::Trim() {
    for (usize i = 0; i < kept_.Size(); ++i) {
        HeapAllocator::Default().Free(kept_[i], kChunkBytes, 64);
    }
    kept_.Clear();
    stats_.pooled = 0;
}

} // namespace engine_placeholder::ecs
