#include "rhi/vulkan/vk_memory.h"

#include "log/assert.h"
#include "rhi/rhi_log.h"
#include "lang/utility.h"
#include "sync/mutex.h"

namespace engine_placeholder::rhi::vulkan
{

namespace
{

u64 AlignUp(u64 value, u64 alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

// Returns the first allowed type satisfying `accept`, preferring the earliest, or -1.
template <typename Accept> int FirstType(const MemoryLayout& layout, u32 type_bits, Accept accept)
{
    for (u32 i = 0; i < layout.type_count; ++i)
    {
        if ((type_bits & (1u << i)) != 0 && accept(layout.types[i]))
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

int SelectMemoryType(const MemoryLayout& layout, MemoryLocation location, u32 type_bits)
{
    int found = -1;
    switch (location)
    {
    case MemoryLocation::GpuOnly:
        found = FirstType(layout, type_bits, [](const MemoryTypeInfo& t) { return t.device_local && !t.host_visible; });
        if (found < 0)
        {
            found = FirstType(layout, type_bits, [](const MemoryTypeInfo& t) { return t.device_local; });
        }
        if (found < 0)
        {
            found = FirstType(layout, type_bits, [](const MemoryTypeInfo&) { return true; });
        }
        return found;
    case MemoryLocation::CpuToGpu:
        found = FirstType(layout, type_bits,
                          [](const MemoryTypeInfo& t) { return t.host_visible && t.host_coherent && !t.device_local; });
        if (found < 0)
        {
            found = FirstType(layout, type_bits,
                              [](const MemoryTypeInfo& t) { return t.host_visible && t.host_coherent; });
        }
        if (found < 0)
        {
            found = FirstType(layout, type_bits, [](const MemoryTypeInfo& t) { return t.host_visible; });
        }
        return found;
    case MemoryLocation::GpuToCpu:
        found = FirstType(layout, type_bits,
                          [](const MemoryTypeInfo& t) { return t.host_visible && t.host_cached && t.host_coherent; });
        if (found < 0)
        {
            found = FirstType(layout, type_bits,
                              [](const MemoryTypeInfo& t) { return t.host_visible && t.host_cached; });
        }
        if (found < 0)
        {
            found = FirstType(layout, type_bits,
                              [](const MemoryTypeInfo& t) { return t.host_visible && t.host_coherent; });
        }
        if (found < 0)
        {
            found = FirstType(layout, type_bits, [](const MemoryTypeInfo& t) { return t.host_visible; });
        }
        return found;
    }
    return -1;
}

// ---- FreeList ----

FreeList::FreeList(u64 size) : capacity_(size), free_bytes_(size)
{
    if (size > 0)
    {
        ranges_.Push({0, size});
    }
}

u64 FreeList::LargestFree() const
{
    u64 largest = 0;
    for (usize i = 0; i < ranges_.Size(); ++i)
    {
        if (ranges_[i].size > largest)
        {
            largest = ranges_[i].size;
        }
    }
    return largest;
}

bool FreeList::Allocate(u64 size, u64 alignment, u64* out_offset)
{
    ENGINE_ASSERT(size > 0, "FreeList::Allocate: size must be non-zero");
    ENGINE_ASSERT(alignment > 0 && (alignment & (alignment - 1)) == 0,
                  "FreeList::Allocate: alignment must be a power of two");
    // Best fit: the range that leaves the least waste after alignment.
    usize best = ranges_.Size();
    u64 best_waste = ~0ull;
    for (usize i = 0; i < ranges_.Size(); ++i)
    {
        const Range& r = ranges_[i];
        u64 aligned = AlignUp(r.offset, alignment);
        if (aligned < r.offset || aligned - r.offset > r.size || r.size - (aligned - r.offset) < size)
        {
            continue;
        }
        u64 waste = r.size - size;
        if (waste < best_waste)
        {
            best_waste = waste;
            best = i;
        }
    }
    if (best == ranges_.Size())
    {
        return false;
    }
    Range r = ranges_[best];
    u64 aligned = AlignUp(r.offset, alignment);
    u64 lead = aligned - r.offset;
    u64 tail = r.size - lead - size;
    // Replace the range with its leading and trailing remainders.
    if (lead > 0 && tail > 0)
    {
        ranges_[best] = {r.offset, lead};
        ranges_.Insert(best + 1, {aligned + size, tail});
    }
    else if (lead > 0)
    {
        ranges_[best] = {r.offset, lead};
    }
    else if (tail > 0)
    {
        ranges_[best] = {aligned + size, tail};
    }
    else
    {
        ranges_.Remove(best);
    }
    free_bytes_ -= size;
    *out_offset = aligned;
    return true;
}

bool FreeList::Free(u64 offset, u64 size)
{
    if (size == 0 || offset > capacity_ || size > capacity_ - offset)
    {
        return false;
    }
    // Find the first range starting at or after the end of the freed one.
    usize i = 0;
    while (i < ranges_.Size() && ranges_[i].offset < offset)
    {
        ++i;
    }
    const bool touches_next = i < ranges_.Size() && ranges_[i].offset == offset + size;
    const bool overlaps_next = i < ranges_.Size() && ranges_[i].offset < offset + size;
    const bool touches_prev = i > 0 && ranges_[i - 1].offset + ranges_[i - 1].size == offset;
    const bool overlaps_prev = i > 0 && ranges_[i - 1].offset + ranges_[i - 1].size > offset;
    if (overlaps_next || overlaps_prev)
    {
        return false;  // the freed range overlaps space that is already free
    }
    if (touches_prev && touches_next)
    {
        ranges_[i - 1].size += size + ranges_[i].size;
        ranges_.Remove(i);
    }
    else if (touches_prev)
    {
        ranges_[i - 1].size += size;
    }
    else if (touches_next)
    {
        ranges_[i].offset = offset;
        ranges_[i].size += size;
    }
    else
    {
        ranges_.Insert(i, {offset, size});
    }
    free_bytes_ += size;
    return true;
}

// ---- MemoryAllocator ----

MemoryAllocator::MemoryAllocator(const MemoryLayout& layout, const MemoryAllocatorConfig& config,
                                 MemoryBackend* backend)
    : layout_(layout), config_(config), backend_(backend)
{
}

MemoryAllocator::~MemoryAllocator()
{
    for (usize i = 0; i < dedicated_.Size(); ++i)
    {
        backend_->Free({dedicated_[i].memory, dedicated_[i].mapped});
    }
    for (Pool& pool : pools_)
    {
        for (usize i = 0; i < pool.blocks.Size(); ++i)
        {
            Block* block = pool.blocks[i].Get();
            if (block->alive)
            {
                backend_->Free({block->memory, block->mapped});
            }
        }
    }
}

u64 MemoryAllocator::BlockSizeFor(u32 memory_type) const
{
    const MemoryTypeInfo& type = layout_.types[memory_type];
    const MemoryHeapInfo& heap = layout_.heaps[type.heap_index];
    u64 size = (heap.device_local && heap.size >= config_.large_heap_bytes) ? config_.large_block_size
                                                                            : config_.block_size;
    // Never let one block take more than an eighth of a small heap.
    u64 cap = heap.size / 8;
    if (cap > 0 && size > cap)
    {
        size = cap;
    }
    return size;
}

MemoryAllocation MemoryAllocator::Allocate(const MemoryRequest& request, MemoryResult* result)
{
    LockGuard<Mutex> lock(mutex_);
    MemoryResult local = MemoryResult::Ok;
    MemoryAllocation allocation = AllocateLocked(request, result != nullptr ? result : &local);
    return allocation;
}

MemoryAllocation MemoryAllocator::AllocateLocked(const MemoryRequest& request_in, MemoryResult* result)
{
    *result = MemoryResult::Failed;
    const MemoryRequest& request_raw = request_in;
    if (request_raw.size == 0)
    {
        RhiLog(LogVerbosity::Error, "GPU memory: zero-size allocation request");
        return {};
    }
    ENGINE_ASSERT(request_raw.alignment > 0 && (request_raw.alignment & (request_raw.alignment - 1)) == 0,
                  "MemoryAllocator::Allocate: alignment must be a power of two");

    int type_index = SelectMemoryType(layout_, request_raw.location, request_raw.memory_type_bits);
    if (type_index < 0)
    {
        RhiLog(LogVerbosity::Error, "GPU memory: no memory type allows location %d with type bits 0x%x",
               static_cast<int>(request_raw.location), request_raw.memory_type_bits);
        return {};
    }
    const u32 type = static_cast<u32>(type_index);
    const MemoryTypeInfo& info = layout_.types[type];
    const bool map = info.host_visible;

    // Non-coherent memory is flushed and invalidated in whole atoms, so a range must not share one with a neighbor.
    MemoryRequest request = request_in;
    if (map && !info.host_coherent && config_.non_coherent_atom_size > 1)
    {
        const u64 atom = config_.non_coherent_atom_size;
        request.size = AlignUp(request.size, atom);
        if (request.alignment < atom)
        {
            request.alignment = atom;
        }
    }

    // Keep headroom below maxMemoryAllocationCount for the objects that must be able to allocate.
    const u32 limit = config_.max_allocation_count > config_.allocation_headroom
                          ? config_.max_allocation_count - config_.allocation_headroom
                          : config_.max_allocation_count;
    const u64 block_size = BlockSizeFor(type);
    const u64 dedicated_threshold = block_size / (config_.dedicated_divisor > 0 ? config_.dedicated_divisor : 1);
    // A huge alignment cannot be satisfied inside a block either, so it also gets its own object.
    const bool dedicated = request.dedicated || request.size > dedicated_threshold ||
                           request.alignment >= dedicated_threshold;

    auto fill = [&](MemoryAllocation* a, const BackendAllocation& mem, u64 offset, u64 size)
    {
        a->memory = mem.handle;
        a->offset = offset;
        a->size = size;
        a->mapped = mem.mapped != nullptr ? static_cast<char*>(mem.mapped) + offset : nullptr;
        a->memory_type = type;
        a->coherent = info.host_coherent;
    };

    if (dedicated)
    {
        if (live_objects_ >= limit)
        {
            RhiLog(LogVerbosity::Error, "GPU memory: allocation-count limit reached (%u live memory objects)",
                   live_objects_);
            LogStatsLocked();
            return {};
        }
        BackendAllocation mem;
        MemoryResult r = backend_->Allocate(type, request.size, map, request.dedicated_buffer, request.dedicated_image,
                                            &mem);
        if (r != MemoryResult::Ok)
        {
            *result = r;
            RhiLog(LogVerbosity::Error, "GPU memory: dedicated allocation of %llu bytes failed (%s)",
                   static_cast<unsigned long long>(request.size),
                   r == MemoryResult::OutOfDeviceMemory ? "out of device memory"
                   : r == MemoryResult::OutOfHostMemory ? "out of host memory"
                                                        : "error");
            LogStatsLocked();
            return {};
        }
        ++live_objects_;
        ++objects_made_;
        dedicated_bytes_[info.heap_index] += request.size;
        ++dedicated_count_[info.heap_index];
        dedicated_.Push({mem.handle, mem.mapped, request.size, info.heap_index});
        MemoryAllocation a;
        fill(&a, mem, 0, request.size);
        a.dedicated = true;
        *result = MemoryResult::Ok;
        return a;
    }

    Pool& pool = pools_[PoolIndex(type, request.linear)];
    // Try the existing blocks, newest first (they are likelier to have room).
    for (usize i = pool.blocks.Size(); i-- > 0;)
    {
        Block* block = pool.blocks[i].Get();
        if (!block->alive)
        {
            continue;
        }
        u64 offset = 0;
        if (block->free_list->Allocate(request.size, request.alignment, &offset))
        {
            block->used += request.size;
            MemoryAllocation a;
            fill(&a, {block->memory, block->mapped}, offset, request.size);
            a.pool = PoolIndex(type, request.linear);
            a.block = static_cast<u32>(i);
            *result = MemoryResult::Ok;
            return a;
        }
    }

    // A new block. Reuse a dead slot in the list if there is one.
    if (live_objects_ >= limit)
    {
        RhiLog(LogVerbosity::Error, "GPU memory: allocation-count limit reached (%u live memory objects)",
               live_objects_);
        LogStatsLocked();
        return {};
    }
    BackendAllocation mem;
    MemoryResult r = backend_->Allocate(type, block_size, map, 0, 0, &mem);
    if (r != MemoryResult::Ok)
    {
        *result = r;
        RhiLog(LogVerbosity::Error, "GPU memory: block allocation of %llu bytes failed (%s)",
               static_cast<unsigned long long>(block_size),
               r == MemoryResult::OutOfDeviceMemory ? "out of device memory"
               : r == MemoryResult::OutOfHostMemory ? "out of host memory"
                                                    : "error");
        LogStatsLocked();
        return {};
    }
    ++live_objects_;
    ++objects_made_;
    UniquePtr<Block> owned = MakeUnique<Block>(mem.handle, mem.mapped, block_size, u64{0},
                                               MakeUnique<FreeList>(block_size), true);
    if (!owned || !owned->free_list)
    {
        RhiLog(LogVerbosity::Error, "GPU memory: out of host memory for a block record");
        backend_->Free({mem.handle, mem.mapped});
        --live_objects_;
        --objects_made_;
        *result = MemoryResult::OutOfHostMemory;
        return {};
    }
    Block* block = owned.Get();
    usize slot = pool.blocks.Size();
    for (usize i = 0; i < pool.blocks.Size(); ++i)
    {
        if (!pool.blocks[i]->alive)
        {
            pool.blocks[i] = Move(owned);  // the dead block's record is destroyed here
            slot = i;
            break;
        }
    }
    if (slot == pool.blocks.Size())
    {
        pool.blocks.Push(Move(owned));
    }
    u64 offset = 0;
    if (!block->free_list->Allocate(request.size, request.alignment, &offset))
    {
        // Should not happen (size and alignment are both below half a block), but never leave the empty block behind.
        RhiLog(LogVerbosity::Error, "GPU memory: request of %llu bytes (alignment %llu) does not fit a fresh block",
               static_cast<unsigned long long>(request.size), static_cast<unsigned long long>(request.alignment));
        backend_->Free({block->memory, block->mapped});
        block->alive = false;
        --live_objects_;
        return {};
    }
    block->used += request.size;
    MemoryAllocation a;
    fill(&a, {block->memory, block->mapped}, offset, request.size);
    a.pool = PoolIndex(type, request.linear);
    a.block = static_cast<u32>(slot);
    *result = MemoryResult::Ok;
    return a;
}

void MemoryAllocator::Free(const MemoryAllocation& allocation)
{
    if (!allocation.Valid())
    {
        return;
    }
    LockGuard<Mutex> lock(mutex_);
    FreeLocked(allocation);
}

void MemoryAllocator::FreeLocked(const MemoryAllocation& allocation)
{
    if (allocation.dedicated)
    {
        usize found = dedicated_.Size();
        for (usize i = 0; i < dedicated_.Size(); ++i)
        {
            if (dedicated_[i].memory == allocation.memory)
            {
                found = i;
                break;
            }
        }
        ENGINE_ASSERT(found < dedicated_.Size(),
                      "MemoryAllocator::Free: dedicated allocation freed twice or not from this allocator");
        if (found == dedicated_.Size())
        {
            return;
        }
        backend_->Free({dedicated_[found].memory, dedicated_[found].mapped});
        --live_objects_;
        --dedicated_count_[dedicated_[found].heap];
        dedicated_bytes_[dedicated_[found].heap] -= dedicated_[found].size;
        dedicated_.Remove(found);
        return;
    }
    ENGINE_ASSERT(allocation.pool < kMaxMemoryTypes * 2,
                  "MemoryAllocator::Free: allocation is not from this allocator");
    Pool& pool = pools_[allocation.pool];
    ENGINE_ASSERT(allocation.block < pool.blocks.Size(),
                  "MemoryAllocator::Free: allocation is not from this allocator");
    Block* block = pool.blocks[allocation.block].Get();
    ENGINE_ASSERT(block->alive && block->memory == allocation.memory,
                  "MemoryAllocator::Free: allocation is not from this allocator");
    bool ok = block->free_list->Free(allocation.offset, allocation.size);
    ENGINE_ASSERT(ok, "MemoryAllocator::Free: double free or range outside the block");
    if (!ok)
    {
        return;  // leave the accounting alone rather than corrupt it
    }
    block->used -= allocation.size;

    // Release an emptied block, unless it is the pool's only empty one (kept as the spare).
    if (block->used == 0)
    {
        usize empty_others = 0;
        for (usize i = 0; i < pool.blocks.Size(); ++i)
        {
            if (i != allocation.block && pool.blocks[i]->alive && pool.blocks[i]->used == 0)
            {
                ++empty_others;
            }
        }
        if (empty_others > 0)
        {
            backend_->Free({block->memory, block->mapped});
            block->alive = false;
            --live_objects_;
        }
    }
}

MemoryStats MemoryAllocator::Stats() const
{
    LockGuard<Mutex> lock(mutex_);
    return StatsLocked();
}

MemoryStats MemoryAllocator::StatsLocked() const
{
    MemoryStats stats;
    stats.heap_count = layout_.heap_count;
    stats.live_memory_objects = live_objects_;
    stats.total_memory_objects_made = objects_made_;
    for (u32 h = 0; h < layout_.heap_count; ++h)
    {
        stats.heaps[h].allocated_bytes = dedicated_bytes_[h];
        stats.heaps[h].used_bytes = dedicated_bytes_[h];
        stats.heaps[h].dedicated_count = dedicated_count_[h];
    }
    for (u32 type = 0; type < layout_.type_count; ++type)
    {
        HeapStats& heap = stats.heaps[layout_.types[type].heap_index];
        for (u32 linear = 0; linear < 2; ++linear)
        {
            const Pool& pool = pools_[type * 2 + linear];
            for (usize i = 0; i < pool.blocks.Size(); ++i)
            {
                const Block* block = pool.blocks[i].Get();
                if (!block->alive)
                {
                    continue;
                }
                heap.allocated_bytes += block->size;
                heap.used_bytes += block->used;
                heap.free_bytes += block->free_list->FreeBytes();
                ++heap.block_count;
                u64 largest = block->free_list->LargestFree();
                if (largest > heap.largest_free_bytes)
                {
                    heap.largest_free_bytes = largest;
                }
            }
        }
    }
    for (u32 h = 0; h < layout_.heap_count; ++h)
    {
        HeapStats& heap = stats.heaps[h];
        heap.fragmentation = heap.free_bytes > 0
                                 ? 1.0f - static_cast<f32>(static_cast<double>(heap.largest_free_bytes) /
                                                           static_cast<double>(heap.free_bytes))
                                 : 0.0f;
    }
    return stats;
}

void MemoryAllocator::LogStats() const
{
    LockGuard<Mutex> lock(mutex_);
    LogStatsLocked();
}

void MemoryAllocator::LogStatsLocked() const
{
    MemoryStats stats = StatsLocked();
    RhiLog(LogVerbosity::Warn, "GPU memory: %u live memory objects (%llu made in total)", stats.live_memory_objects,
           static_cast<unsigned long long>(stats.total_memory_objects_made));
    for (u32 h = 0; h < stats.heap_count; ++h)
    {
        const HeapStats& heap = stats.heaps[h];
        RhiLog(LogVerbosity::Warn,
               "GPU memory: heap %u%s: %llu MiB allocated, %llu MiB used, %u blocks, %u dedicated, fragmentation %.2f",
               h, layout_.heaps[h].device_local ? " (device-local)" : "",
               static_cast<unsigned long long>(heap.allocated_bytes >> 20),
               static_cast<unsigned long long>(heap.used_bytes >> 20), heap.block_count, heap.dedicated_count,
               static_cast<double>(heap.fragmentation));
    }
}

}  // namespace engine_placeholder::rhi::vulkan
