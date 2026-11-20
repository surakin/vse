// Counts every heap allocation in the ECS test executable so a test can prove that a region allocates none (iterating a
// query). The ThreadSanitizer runtime defines its own operator new (a second definition fails to link), so that flavor
// does not count.
#include "alloc_counter.h"

#include "build_defs.h"
#include "cstr/mem_ops.h"
#include "lang/new.h"
#include "sync/atomic.h"
#include "types.h"

using namespace vse;

#if ENGINE_SANITIZER_THREAD
#define ECS_TEST_NO_NEW_COUNTER 1
#endif

static Atomic<unsigned long> g_heap_allocations{0};

#ifdef ECS_TEST_NO_NEW_COUNTER
bool EcsTestCountsAllocations()
{
    return false;
}
#else
bool EcsTestCountsAllocations()
{
    return true;
}
#endif
unsigned long EcsTestHeapAllocations()
{
    return g_heap_allocations.Load();
}

#ifndef ECS_TEST_NO_NEW_COUNTER
// Every form is replaced so allocation and release pair up (sanitizers check that). `align` is 0 for the plain forms.
// malloc gives 16 bytes of alignment, so a block that needs more is over-allocated: its start is rounded up and the
// underlying pointer is kept just before it.
static constexpr usize kMallocAlign = 16;
static void* CountedAlloc(usize size, usize align)
{
    g_heap_allocations.FetchAdd(1, MemoryOrder::Relaxed);
    if (size == 0)
    {
        size = 1;
    }
    if (align <= kMallocAlign)
    {
        return MemAllocate(size);
    }
    void* raw = MemAllocate(size + align + sizeof(void*));
    if (raw == nullptr)
    {
        return nullptr;
    }
    const usize start = (reinterpret_cast<usize>(raw) + sizeof(void*) + align - 1) / align * align;
    void* block = reinterpret_cast<void*>(start);
    reinterpret_cast<void**>(block)[-1] = raw;
    return block;
}
static void AlignedFree(void* p, usize align) noexcept
{
    if (align <= kMallocAlign)
    {
        MemFree(p);
    }
    else if (p != nullptr)
    {
        MemFree(reinterpret_cast<void**>(p)[-1]);
    }
}
void* operator new(usize n)
{
    if (void* p = CountedAlloc(n, 0))
    {
        return p;
    }
    throw BadAlloc();
}
void* operator new[](usize n)
{
    if (void* p = CountedAlloc(n, 0))
    {
        return p;
    }
    throw BadAlloc();
}
void* operator new(usize n, const NoThrowTag&) noexcept
{
    return CountedAlloc(n, 0);
}
void* operator new[](usize n, const NoThrowTag&) noexcept
{
    return CountedAlloc(n, 0);
}
void* operator new(usize n, AlignVal a)
{
    if (void* p = CountedAlloc(n, static_cast<usize>(a)))
    {
        return p;
    }
    throw BadAlloc();
}
void* operator new[](usize n, AlignVal a)
{
    if (void* p = CountedAlloc(n, static_cast<usize>(a)))
    {
        return p;
    }
    throw BadAlloc();
}
void* operator new(usize n, AlignVal a, const NoThrowTag&) noexcept
{
    return CountedAlloc(n, static_cast<usize>(a));
}
void* operator new[](usize n, AlignVal a, const NoThrowTag&) noexcept
{
    return CountedAlloc(n, static_cast<usize>(a));
}
void operator delete(void* p) noexcept
{
    MemFree(p);
}
void operator delete[](void* p) noexcept
{
    MemFree(p);
}
void operator delete(void* p, usize) noexcept
{
    MemFree(p);
}
void operator delete[](void* p, usize) noexcept
{
    MemFree(p);
}
void operator delete(void* p, const NoThrowTag&) noexcept
{
    MemFree(p);
}
void operator delete[](void* p, const NoThrowTag&) noexcept
{
    MemFree(p);
}
void operator delete(void* p, AlignVal a) noexcept
{
    AlignedFree(p, static_cast<usize>(a));
}
void operator delete[](void* p, AlignVal a) noexcept
{
    AlignedFree(p, static_cast<usize>(a));
}
void operator delete(void* p, AlignVal a, const NoThrowTag&) noexcept
{
    AlignedFree(p, static_cast<usize>(a));
}
void operator delete[](void* p, AlignVal a, const NoThrowTag&) noexcept
{
    AlignedFree(p, static_cast<usize>(a));
}
void operator delete(void* p, usize, AlignVal a) noexcept
{
    AlignedFree(p, static_cast<usize>(a));
}
void operator delete[](void* p, usize, AlignVal a) noexcept
{
    AlignedFree(p, static_cast<usize>(a));
}
#endif
