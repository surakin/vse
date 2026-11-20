# Foundation: Memory

Status: written (Phase 0; allocators as they are in Phase 17).

**Audience:** every engine programmer. Read [Standard types](foundation-types.md) first for the vocabulary, and [Containers](foundation-containers.md) after this
page: the containers are what normally use the allocators.

The engine does not call `malloc` or `new` in ordinary code. Memory comes from an **allocator**, an object you pass to whatever needs memory. This page
explains the four you will meet, how to choose one, and the rules that keep them safe.

## Why allocators?

A game spends its life repeating the same work, once per frame. Most memory it needs lives for exactly that long: the list of visible objects, the
scratch text of a log line, a sorted copy of some data. With a general heap each of these is a `malloc` and a `free`, each slow and each a chance to
fragment memory. With an allocator that knows the lifetime, the same memory costs a pointer increment to get and nothing at all to free: you throw the
whole block away at once.

So the question to ask whenever you need memory is **"how long does this live, and who frees it?"**

## Choosing an allocator

| If the memory... | Use | Cost to allocate | Cost to free |
|---|---|---|---|
| lives as long as nothing else can tell (general data, containers that grow) | [[HeapAllocator]] (the default of `Vector`) | a heap call | a heap call, one block at a time |
| all dies together (one frame, one load, one command line) | [[ArenaAllocator]] | a pointer bump | `Reset()` for all of it |
| dies in the reverse order it was made (nested scopes) | [[StackAllocator]] | a pointer bump | `PopToMarker(marker)` |
| is many objects of one size that come and go individually | [[PoolAllocator]] | a free-list pop | a free-list push |

When you are not sure, use the heap (just write `Vector<T>`): it is correct everywhere. Reach for an arena when you notice a group of allocations that
all end at the same moment.

## The allocator contract

An allocator in the engine is any type with `void* Alloc(usize size, usize align)`. Containers such as [[Vector]], [[HashMap]] and [[StringBuilder]] are
templates over it and hold a **pointer to the allocator**, so the allocator **must outlive** every container using it. A container whose allocator also has
`Free` (the heap) releases its storage; one whose allocator has none (the arena) never gives memory back until the allocator is reset.

Memory from an allocator is **not zeroed** and **no constructors or destructors run**. The containers construct and destroy their elements; if you take raw
memory from an allocator you construct objects in it yourself with `ConstructAt<T>(where, args...)` (`lang/new.h`) and destroy them yourself.

## HeapAllocator

The general-purpose allocator over the C++ aligned heap. It has no state: `HeapAllocator::Default()` is the shared instance, and `Vector<T>` uses it when you
do not pass one. It is thread-safe (the heap is). `Alloc` returns `nullptr` when the heap is exhausted; the containers turn that into an abort (`Resize`) or a
`false` (`TryResize`), see [Containers](foundation-containers.md).

## ArenaAllocator: one frame, one load

An arena reserves one block of virtual memory when it is created and hands out pieces by moving a pointer forward. Nothing is freed individually; `Reset()`
makes the whole block reusable and `FreeAll()` (or the destructor) gives it back to the operating system.

```cpp compile run standalone
#include "containers/vector.h"
#include "mem/arena_allocator.h"
#include "os/console.h"

using namespace vse;

int main() {
    ArenaAllocator frame(64 * 1024);          // one block, reserved once

    for (u32 frame_number = 0; frame_number < 3; ++frame_number) {
        {
            Vector<u32, ArenaAllocator> visible(frame);   // draws from the arena
            for (u32 i = 0; i < 1000; ++i) {
                visible.Push(i * frame_number);
            }
            os::Print("frame %u: %zu items\n", frame_number, visible.Size());
        }                                      // `visible` is gone before the reset
        frame.Reset();                         // the whole frame's memory is free again
    }

    void* block = frame.Alloc(1 << 20, 16);    // asking for more than is left
    os::Print("1 MiB from a 64 KiB arena: %s\n", block == nullptr ? "nullptr" : "ok");
    return 0;
}
```

```output
frame 0: 1000 items
frame 1: 1000 items
frame 2: 1000 items
1 MiB from a 64 KiB arena: nullptr
```

Things to know:

- **Exhaustion is `nullptr`, not a crash.** Check it when the size depends on data you do not control. Containers over an arena abort when the arena is full,
  because growing a `Vector` cannot continue without memory.
- **A growing `Vector` wastes arena space.** Each growth gets a new, bigger block and the old one stays in the arena until `Reset`. `Reserve` once if you
  know the size.
- **`Reset` invalidates everything.** Every pointer, span and container that used the arena is dead afterwards. Destroy or stop using them first.
- **Destructors do not run.** An arena holds plain data and containers' storage. Do not put objects that own resources (files, other allocations) in raw
  arena memory unless you destroy them yourself before the reset.
- Not thread-safe: give each thread its own arena, or lock around it.

## StackAllocator: nested scopes

The same storage as an arena, but you can rewind to a saved position:

```cpp compile run standalone
#include "mem/stack_allocator.h"
#include "os/console.h"

using namespace vse;

int main() {
    StackAllocator stack(4096);
    void* outer = stack.Push(100, 16);

    const StackAllocator::Marker mark = stack.CurrentMarker();
    void* inner_a = stack.Push(200, 16);   // temporary work
    void* inner_b = stack.Push(300, 16);
    stack.PopToMarker(mark);               // both inner blocks are gone, `outer` is untouched

    void* reused = stack.Push(200, 16);
    os::Print("outer kept: %s, inner space reused: %s\n", outer != nullptr ? "yes" : "no", reused == inner_a ? "yes" : "no");
    (void)inner_b;
    return 0;
}
```

```output
outer kept: yes, inner space reused: yes
```

Use it for temporary work inside a function that may run many times a frame. Popping to a marker you have already popped past is not checked and corrupts
the order, so take markers in a disciplined, nested way (a small RAII wrapper in your own code is a good idea).

## PoolAllocator: many objects of one size

A pool is one block cut into equal slots, with a free list threaded through the unused ones. `Alloc()` and `Free(ptr)` are both O(1) and any slot can be freed
at any time, so it suits objects that are created and destroyed one by one (nodes, small records). It cannot grow: when every slot is taken `Alloc` returns
`nullptr`. Freeing a pointer that did not come from the pool, or freeing twice, corrupts it and is not checked.

```cpp compile run standalone
#include "mem/pool_allocator.h"
#include "os/console.h"

using namespace vse;

int main() {
    PoolAllocator pool(sizeof(u64), 2);   // two 8-byte slots
    void* a = pool.Alloc();
    void* b = pool.Alloc();
    void* none = pool.Alloc();
    os::Print("third allocation: %s\n", none == nullptr ? "nullptr (pool full)" : "ok");
    pool.Free(a);
    void* again = pool.Alloc();
    os::Print("a freed slot is reused: %s\n", again == a ? "yes" : "no");
    (void)b;
    return 0;
}
```

```output
third allocation: nullptr (pool full)
a freed slot is reused: yes
```

The job system uses its own thread-safe `FramePool`, not `PoolAllocator`; this one is for single-threaded code.

## Reserved and committed memory

Arena, stack and pool take their block from the operating system with `os::ReserveMemory` (`os/os.h`): address space is reserved up front and the
system hands out real memory as pages are touched. A 1 GiB arena therefore costs address space, not 1 GiB of RAM, until you use it. If the system
refuses the reservation the allocator is created with zero capacity and every allocation returns `nullptr`.

## Common mistakes

- **Using a container after its arena was reset.** The container still has a pointer into reused memory. Scope the containers inside the frame.
- **Letting an allocator die before its containers.** Declare the allocator first (it is destroyed last).
- **Expecting zeroed memory.** Use `Resize` (value-initializes) or `MemSet` yourself.
- **Putting a destructor-owning object in an arena and resetting.** The destructor never runs; the resource leaks.
- **Sharing an arena between threads.** It is not thread-safe.
- **Calling `new` or `malloc`.** The `raw_new_check` and `std_usage_check` gates fail the build.

## See also

[Containers](foundation-containers.md), [Standard types](foundation-types.md), reference: [Foundation: Memory](../reference/foundation-memory.md).
