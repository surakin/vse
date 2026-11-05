#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gpu_fixture.h"
#include "rhi/rhi.h"
#include "rhi/vulkan/vk_memory.h"
#include "rhi/vulkan/vulkan_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;

namespace {

// A fake memory backend: hands out ids, keeps host memory for mapped objects, can be told to fail.
class FakeBackend final : public MemoryBackend {
public:
    MemoryResult Allocate(u32 type, u64 size, bool map, u64 dedicated_buffer, u64 dedicated_image, BackendAllocation* out) override {
        if (fail_next != MemoryResult::Ok) {
            MemoryResult r = fail_next;
            fail_next = MemoryResult::Ok;
            return r;
        }
        if (budget != 0 && live_bytes + size > budget) {
            return MemoryResult::OutOfDeviceMemory;
        }
        ++calls;
        last_type = type;
        last_dedicated_buffer = dedicated_buffer;
        last_dedicated_image = dedicated_image;
        u64 id = next_id++;
        live[id] = size;
        live_bytes += size;
        out->handle = id;
        out->mapped = nullptr;
        if (map) {
            storage[id].resize(size);
            out->mapped = storage[id].data();
        }
        return MemoryResult::Ok;
    }
    void Free(const BackendAllocation& a) override {
        auto it = live.find(a.handle);
        REQUIRE(it != live.end()); // a double free of a whole object would fail here
        live_bytes -= it->second;
        live.erase(it);
        storage.erase(a.handle);
        ++frees;
    }
    MemoryResult fail_next = MemoryResult::Ok;
    u64 budget = 0;
    u64 calls = 0, frees = 0, live_bytes = 0, next_id = 1;
    u32 last_type = 0;
    u64 last_dedicated_buffer = 0, last_dedicated_image = 0;
    std::map<u64, u64> live;
    std::map<u64, std::vector<char>> storage;
};

// Type 0: device-local; 1: host-visible coherent; 2: host-visible cached; 3: device-local + host-visible (BAR).
MemoryLayout TestLayout() {
    MemoryLayout l;
    l.type_count = 4;
    l.types[0] = {0, true, false, false, false};
    l.types[1] = {1, false, true, true, false};
    l.types[2] = {1, false, true, false, true};
    l.types[3] = {0, true, true, true, false};
    l.heap_count = 2;
    l.heaps[0] = {8ull << 30, true};
    l.heaps[1] = {16ull << 30, false};
    return l;
}

} // namespace

TEST_CASE("SelectMemoryType follows the documented preferences and fallbacks", "[rhi][memory]") {
    MemoryLayout l = TestLayout();
    CHECK(SelectMemoryType(l, MemoryLocation::GpuOnly, ~0u) == 0);
    CHECK(SelectMemoryType(l, MemoryLocation::CpuToGpu, ~0u) == 1);
    CHECK(SelectMemoryType(l, MemoryLocation::GpuToCpu, ~0u) == 2);

    // Type bits restrict the choice.
    CHECK(SelectMemoryType(l, MemoryLocation::GpuOnly, 0b1000) == 3); // only the BAR type is allowed: device-local fallback
    CHECK(SelectMemoryType(l, MemoryLocation::CpuToGpu, 0b1000) == 3); // resizable-BAR fallback
    CHECK(SelectMemoryType(l, MemoryLocation::CpuToGpu, 0b0100) == 2); // non-coherent host-visible as a last resort
    CHECK(SelectMemoryType(l, MemoryLocation::CpuToGpu, 0b0001) == -1); // nothing host-visible allowed
    CHECK(SelectMemoryType(l, MemoryLocation::GpuToCpu, 0b0010) == 1); // coherent, uncached
    CHECK(SelectMemoryType(l, MemoryLocation::GpuToCpu, 0b0001) == -1);

    // Unified memory: one type that is everything.
    MemoryLayout unified;
    unified.type_count = 1;
    unified.types[0] = {0, true, true, true, true};
    unified.heap_count = 1;
    unified.heaps[0] = {4ull << 30, true};
    CHECK(SelectMemoryType(unified, MemoryLocation::GpuOnly, ~0u) == 0);
    CHECK(SelectMemoryType(unified, MemoryLocation::CpuToGpu, ~0u) == 0);
    CHECK(SelectMemoryType(unified, MemoryLocation::GpuToCpu, ~0u) == 0);
}

TEST_CASE("FreeList allocates with alignment and coalesces on free", "[rhi][memory]") {
    FreeList list(1024);
    u64 a = 0, b = 0, c = 0;
    REQUIRE(list.Allocate(100, 1, &a));
    CHECK(a == 0);
    REQUIRE(list.Allocate(100, 256, &b));
    CHECK(b == 256); // aligned up; the gap [100,256) stays free
    CHECK(list.RangeCount() == 2);
    REQUIRE(list.Allocate(10, 1, &c));
    CHECK(c == 100); // best fit takes the small gap
    CHECK(list.FreeBytes() == 1024 - 100 - 100 - 10);

    CHECK(list.Free(a, 100));
    CHECK(list.Free(c, 10));
    CHECK(list.Free(b, 100));
    CHECK(list.RangeCount() == 1); // everything merged back
    CHECK(list.FreeBytes() == 1024);
    CHECK(list.LargestFree() == 1024);
}

TEST_CASE("FreeList reports exhaustion and double frees without changing state", "[rhi][memory]") {
    FreeList list(256);
    u64 a = 0, b = 0;
    REQUIRE(list.Allocate(256, 1, &a));
    CHECK_FALSE(list.Allocate(1, 1, &b));
    CHECK(list.FreeBytes() == 0);

    CHECK(list.Free(a, 256));
    CHECK_FALSE(list.Free(a, 256));     // double free
    CHECK_FALSE(list.Free(100, 10));    // overlaps free space
    CHECK_FALSE(list.Free(250, 100));   // outside the block
    CHECK_FALSE(list.Free(0, 0));       // empty
    CHECK(list.FreeBytes() == 256);
    CHECK(list.RangeCount() == 1);

    REQUIRE(list.Allocate(100, 64, &a));
    REQUIRE(list.Allocate(100, 64, &b));
    CHECK(list.Free(a, 100));
    CHECK_FALSE(list.Free(a + 50, 20)); // inside a freed range
    CHECK(list.Free(b, 100));
}

TEST_CASE("FreeList survives 100k random allocate/free cycles with its invariants intact", "[rhi][memory][stress]") {
    constexpr u64 kSize = 1 << 20;
    FreeList list(kSize);
    struct Live {
        u64 offset, size;
    };
    std::vector<Live> live;
    u64 state = 0x9E3779B97F4A7C15ull;
    auto next = [&]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    for (int step = 0; step < 100000; ++step) {
        if (live.empty() || (next() % 3 != 0 && live.size() < 2000)) {
            u64 size = 1 + next() % 4096;
            u64 alignment = u64(1) << (next() % 9);
            u64 offset = 0;
            if (list.Allocate(size, alignment, &offset)) {
                REQUIRE(offset % alignment == 0);
                REQUIRE(offset + size <= kSize);
                live.push_back({offset, size});
            }
        } else {
            usize i = static_cast<usize>(next() % live.size());
            REQUIRE(list.Free(live[i].offset, live[i].size));
            live[i] = live.back();
            live.pop_back();
        }
        if (step % 5000 == 0) {
            // Total bytes are conserved, free ranges are ordered, non-adjacent and disjoint from live ranges.
            u64 used = 0;
            for (const Live& l : live) {
                used += l.size;
            }
            REQUIRE(list.FreeBytes() + used == kSize);
            u64 free_sum = 0;
            for (usize r = 0; r < list.RangeCount(); ++r) {
                free_sum += list.RangeSize(r);
                if (r > 0) {
                    REQUIRE(list.RangeOffset(r) > list.RangeOffset(r - 1) + list.RangeSize(r - 1)); // gap = a live range
                }
                for (const Live& l : live) {
                    bool disjoint = l.offset + l.size <= list.RangeOffset(r) || list.RangeOffset(r) + list.RangeSize(r) <= l.offset;
                    REQUIRE(disjoint);
                }
            }
            REQUIRE(free_sum == list.FreeBytes());
        }
    }
    for (const Live& l : live) {
        REQUIRE(list.Free(l.offset, l.size));
    }
    CHECK(list.RangeCount() == 1);
    CHECK(list.FreeBytes() == kSize);
}

TEST_CASE("MemoryAllocator suballocates small requests from shared blocks", "[rhi][memory]") {
    FakeBackend backend;
    MemoryAllocatorConfig config;
    config.block_size = 1 << 20;
    config.large_heap_bytes = ~0ull; // never use the large block size
    MemoryAllocator allocator(TestLayout(), config, &backend);

    MemoryRequest request;
    request.size = 1000;
    request.alignment = 256;
    std::vector<MemoryAllocation> allocations;
    for (int i = 0; i < 1000; ++i) {
        MemoryResult r = MemoryResult::Failed;
        MemoryAllocation a = allocator.Allocate(request, &r);
        REQUIRE(r == MemoryResult::Ok);
        REQUIRE(a.Valid());
        CHECK(a.offset % 256 == 0);
        CHECK(a.mapped == nullptr);
        CHECK_FALSE(a.dedicated);
        allocations.push_back(a);
    }
    // 1000 * 1024 (aligned stride) bytes ≈ 1 MiB: a handful of blocks, not 1000 objects.
    CHECK(backend.calls <= 3);

    MemoryStats stats = allocator.Stats();
    CHECK(stats.heaps[0].used_bytes == 1000 * 1000);
    CHECK(stats.heaps[0].block_count >= 1);
    CHECK(stats.live_memory_objects == backend.live.size());

    for (const MemoryAllocation& a : allocations) {
        allocator.Free(a);
    }
    stats = allocator.Stats();
    CHECK(stats.heaps[0].used_bytes == 0);
    CHECK(backend.live.size() == 1); // one empty block is kept as the spare
}

TEST_CASE("MemoryAllocator maps host-visible blocks once and gives each range its own pointer", "[rhi][memory]") {
    FakeBackend backend;
    MemoryAllocatorConfig config;
    config.block_size = 1 << 20;
    config.large_heap_bytes = ~0ull; // never use the large block size
    MemoryAllocator allocator(TestLayout(), config, &backend);
    MemoryRequest request;
    request.size = 4096;
    request.alignment = 16;
    request.location = MemoryLocation::CpuToGpu;
    MemoryAllocation a = allocator.Allocate(request, nullptr);
    MemoryAllocation b = allocator.Allocate(request, nullptr);
    REQUIRE(a.Valid());
    REQUIRE(b.Valid());
    CHECK(a.mapped != nullptr);
    CHECK(a.coherent);
    CHECK(a.memory == b.memory); // same block
    CHECK(static_cast<char*>(b.mapped) - static_cast<char*>(a.mapped) == static_cast<long>(b.offset - a.offset));
    CHECK(backend.calls == 1);
    allocator.Free(a);
    allocator.Free(b);
}

TEST_CASE("MemoryAllocator keeps linear and optimal resources in separate blocks", "[rhi][memory]") {
    FakeBackend backend;
    MemoryAllocatorConfig config;
    config.block_size = 1 << 20;
    config.large_heap_bytes = ~0ull; // never use the large block size
    MemoryAllocator allocator(TestLayout(), config, &backend);
    MemoryRequest request;
    request.size = 256;
    request.linear = true;
    MemoryAllocation linear = allocator.Allocate(request, nullptr);
    request.linear = false;
    MemoryAllocation optimal = allocator.Allocate(request, nullptr);
    CHECK(linear.memory != optimal.memory);
    CHECK(backend.calls == 2);
    allocator.Free(linear);
    allocator.Free(optimal);
}

TEST_CASE("MemoryAllocator gives large and dedicated requests their own memory object", "[rhi][memory]") {
    FakeBackend backend;
    MemoryAllocatorConfig config;
    config.block_size = 1 << 20;
    config.large_heap_bytes = ~0ull; // never use the large block size
    MemoryAllocator allocator(TestLayout(), config, &backend);

    MemoryRequest big;
    big.size = (1 << 19) + 1; // over half a block
    MemoryAllocation a = allocator.Allocate(big, nullptr);
    REQUIRE(a.Valid());
    CHECK(a.dedicated);
    CHECK(a.offset == 0);
    CHECK(a.size == big.size);

    MemoryRequest asked;
    asked.size = 64;
    asked.dedicated = true;
    asked.dedicated_buffer = 0xB0F;
    MemoryAllocation b = allocator.Allocate(asked, nullptr);
    CHECK(b.dedicated);
    CHECK(backend.last_dedicated_buffer == 0xB0F);

    MemoryStats stats = allocator.Stats();
    CHECK(stats.heaps[0].dedicated_count == 2);
    CHECK(stats.heaps[0].allocated_bytes == big.size + 64);
    allocator.Free(a);
    allocator.Free(b);
    CHECK(backend.live.empty());
    CHECK(allocator.Stats().heaps[0].dedicated_count == 0);
}

TEST_CASE("MemoryAllocator failure policy: empty allocation, the right reason, no abort", "[rhi][memory]") {
    FakeBackend backend;
    MemoryAllocatorConfig config;
    config.block_size = 1 << 20;
    config.large_heap_bytes = ~0ull; // never use the large block size
    MemoryAllocator allocator(TestLayout(), config, &backend);
    MemoryRequest request;
    request.size = 1024;
    MemoryResult result = MemoryResult::Ok;

    backend.fail_next = MemoryResult::OutOfDeviceMemory;
    CHECK_FALSE(allocator.Allocate(request, &result).Valid());
    CHECK(result == MemoryResult::OutOfDeviceMemory);

    backend.fail_next = MemoryResult::OutOfHostMemory;
    CHECK_FALSE(allocator.Allocate(request, &result).Valid());
    CHECK(result == MemoryResult::OutOfHostMemory);

    request.memory_type_bits = 0; // no allowed type
    CHECK_FALSE(allocator.Allocate(request, &result).Valid());
    CHECK(result == MemoryResult::Failed);

    request.memory_type_bits = ~0u;
    request.size = 0;
    CHECK_FALSE(allocator.Allocate(request, &result).Valid());

    // After failures the allocator still works.
    request.size = 1024;
    CHECK(allocator.Allocate(request, &result).Valid());
    CHECK(result == MemoryResult::Ok);
}

TEST_CASE("MemoryAllocator tracks dedicated objects: teardown frees them", "[rhi][memory]") {
    FakeBackend backend;
    MemoryAllocatorConfig config;
    config.block_size = 1 << 20;
    config.large_heap_bytes = ~0ull;
    {
        MemoryAllocator allocator(TestLayout(), config, &backend);
        MemoryRequest request;
        request.size = 800 << 10;
        MemoryAllocation held = allocator.Allocate(request, nullptr);
        REQUIRE(held.dedicated);
        request.size = 100;
        MemoryAllocation small = allocator.Allocate(request, nullptr);
        REQUIRE(small.Valid());
        CHECK(backend.live.size() == 2);
        // Neither is freed before the allocator goes away.
    }
    CHECK(backend.live.empty());
}

TEST_CASE("MemoryAllocator rounds ranges in non-coherent memory to the atom size", "[rhi][memory]") {
    FakeBackend backend;
    MemoryAllocatorConfig config;
    config.block_size = 1 << 20;
    config.large_heap_bytes = ~0ull;
    config.non_coherent_atom_size = 256;
    MemoryAllocator allocator(TestLayout(), config, &backend);

    MemoryRequest request;
    request.size = 100;
    request.alignment = 4;
    request.location = MemoryLocation::GpuToCpu; // type 2: host-visible, cached, not coherent
    MemoryAllocation a = allocator.Allocate(request, nullptr);
    MemoryAllocation b = allocator.Allocate(request, nullptr);
    REQUIRE(a.Valid());
    REQUIRE(b.Valid());
    CHECK_FALSE(a.coherent);
    CHECK(a.size == 256);
    CHECK(a.offset % 256 == 0);
    CHECK(b.offset % 256 == 0);
    CHECK(b.offset >= a.offset + a.size); // no shared atom

    request.location = MemoryLocation::CpuToGpu; // coherent: untouched
    request.size = 100;
    MemoryAllocation c = allocator.Allocate(request, nullptr);
    CHECK(c.size == 100);
    allocator.Free(a);
    allocator.Free(b);
    allocator.Free(c);
}

TEST_CASE("MemoryAllocator sends a huge alignment to a dedicated object instead of leaking an empty block", "[rhi][memory]") {
    FakeBackend backend;
    MemoryAllocatorConfig config;
    config.block_size = 1 << 20;
    config.large_heap_bytes = ~0ull;
    MemoryAllocator allocator(TestLayout(), config, &backend);
    MemoryRequest request;
    request.size = 64;
    request.alignment = 1 << 20;
    MemoryAllocation a = allocator.Allocate(request, nullptr);
    REQUIRE(a.Valid());
    CHECK(a.dedicated);
    CHECK(backend.live.size() == 1);
    allocator.Free(a);
    CHECK(backend.live.empty());
}

TEST_CASE("MemoryAllocator respects the allocation-count limit and its headroom", "[rhi][memory]") {
    FakeBackend backend;
    MemoryAllocatorConfig config;
    config.block_size = 1 << 20;
    config.large_heap_bytes = ~0ull; // never use the large block size
    config.max_allocation_count = 10;
    config.allocation_headroom = 4; // so only 6 objects may exist
    MemoryAllocator allocator(TestLayout(), config, &backend);
    MemoryRequest request;
    request.size = 600 << 10; // dedicated, one object each
    std::vector<MemoryAllocation> held;
    for (int i = 0; i < 6; ++i) {
        MemoryAllocation a = allocator.Allocate(request, nullptr);
        REQUIRE(a.Valid());
        held.push_back(a);
    }
    MemoryResult result = MemoryResult::Ok;
    CHECK_FALSE(allocator.Allocate(request, &result).Valid());
    CHECK(result == MemoryResult::Failed);
    CHECK(backend.live.size() == 6);
    allocator.Free(held.back());
    held.pop_back();
    CHECK(allocator.Allocate(request, nullptr).Valid()); // freeing makes room again
}

TEST_CASE("MemoryAllocator releases emptied blocks but keeps one spare", "[rhi][memory]") {
    FakeBackend backend;
    MemoryAllocatorConfig config;
    config.block_size = 1 << 20;
    config.large_heap_bytes = ~0ull; // never use the large block size
    MemoryAllocator allocator(TestLayout(), config, &backend);
    MemoryRequest request;
    request.size = 400 << 10; // two fit per 1 MiB block
    std::vector<MemoryAllocation> held;
    for (int i = 0; i < 6; ++i) {
        held.push_back(allocator.Allocate(request, nullptr));
    }
    CHECK(backend.live.size() == 3);
    for (const MemoryAllocation& a : held) {
        allocator.Free(a);
    }
    CHECK(backend.live.size() == 1); // two blocks released, one spare kept
    // The spare is reused without a new backend call.
    u64 calls_before = backend.calls;
    MemoryAllocation again = allocator.Allocate(request, nullptr);
    CHECK(again.Valid());
    CHECK(backend.calls == calls_before);
    allocator.Free(again);
}

TEST_CASE("MemoryAllocator statistics estimate fragmentation", "[rhi][memory]") {
    FakeBackend backend;
    MemoryAllocatorConfig config;
    config.block_size = 1 << 20;
    config.large_heap_bytes = ~0ull; // never use the large block size
    MemoryAllocator allocator(TestLayout(), config, &backend);
    MemoryRequest request;
    request.size = 64 << 10;
    std::vector<MemoryAllocation> held;
    for (int i = 0; i < 16; ++i) {
        held.push_back(allocator.Allocate(request, nullptr));
    }
    CHECK(allocator.Stats().heaps[0].fragmentation == 0.0f); // block is full
    for (int i = 0; i < 16; i += 2) {
        allocator.Free(held[i]); // free every other range: eight 64 KiB holes
    }
    MemoryStats stats = allocator.Stats();
    CHECK(stats.heaps[0].free_bytes == 8 * (64 << 10));
    CHECK(stats.heaps[0].largest_free_bytes == 64 << 10);
    CHECK(stats.heaps[0].fragmentation > 0.8f);
    for (int i = 1; i < 16; i += 2) {
        allocator.Free(held[i]);
    }
}

TEST_CASE("Real driver: allocations of each location kind", "[rhi][memory][gpu]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    DeviceCreateInfo info;
    info.validation = ValidationSetting::On;
    VulkanDevicePtr device = VulkanDevice::Create(info);
    if (device == nullptr) {
        if (gpu_test::GpuRequired()) {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan device could be created");
        }
        SKIP("no suitable Vulkan device");
    }
    MemoryAllocator& memory = device->Memory();

    MemoryRequest request;
    request.size = 4096;
    request.alignment = 256;

    request.location = MemoryLocation::GpuOnly;
    MemoryAllocation gpu = memory.Allocate(request, nullptr);
    REQUIRE(gpu.Valid());

    request.location = MemoryLocation::CpuToGpu;
    MemoryAllocation up = memory.Allocate(request, nullptr);
    REQUIRE(up.Valid());
    REQUIRE(up.mapped != nullptr);
    std::memset(up.mapped, 0xAB, 4096); // the mapping is writable

    request.location = MemoryLocation::GpuToCpu;
    MemoryAllocation down = memory.Allocate(request, nullptr);
    REQUIRE(down.Valid());
    CHECK(down.mapped != nullptr);

    memory.Free(gpu);
    memory.Free(up);
    memory.Free(down);

    u64 budget = 0, usage = 0;
    if (device->HeapBudget(0, &budget, &usage)) {
        CHECK(budget > 0);
    }
    device.Reset();
}

TEST_CASE("Real driver: a flood of small allocations needs only a few memory objects", "[rhi][memory][gpu]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    DeviceCreateInfo info;
    info.validation = ValidationSetting::On;
    VulkanDevicePtr device = VulkanDevice::Create(info);
    if (device == nullptr) {
        SKIP("no suitable Vulkan device");
    }
    MemoryAllocator& memory = device->Memory();
    const u64 before = device->MemoryObjectsAllocated();

    // 2048 allocations of 256 KiB = 512 MiB of device-local memory, never touched.
    MemoryRequest request;
    request.size = 256 << 10;
    request.alignment = 4096;
    std::vector<MemoryAllocation> held;
    for (int i = 0; i < 2048; ++i) {
        MemoryAllocation a = memory.Allocate(request, nullptr);
        if (!a.Valid()) {
            break; // a GPU with little free memory: judge what was achieved
        }
        held.push_back(a);
    }
    REQUIRE(held.size() >= 256); // at least 64 MiB
    const u64 objects = device->MemoryObjectsAllocated() - before;
    CHECK(objects <= 1 + held.size() / ((64 << 20) / request.size) + 1);
    CHECK(objects < 16);
    for (const MemoryAllocation& a : held) {
        memory.Free(a);
    }
    device.Reset();
}
