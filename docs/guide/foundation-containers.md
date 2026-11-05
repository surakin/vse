# Foundation: Containers

> **Status:** skeleton — not yet written  
> **Audience:** engine programmers  
> **Covers:** Phase 0

## Source of truth

- `engine/foundation/containers/array.h`
- `engine/foundation/containers/unique_ptr.h`
- `engine/foundation/containers/shared_ptr.h`
- `engine/foundation/containers/vector.h`
- `engine/foundation/mem/heap_allocator.h`
- `engine/foundation/containers/hash_map.h`

## API reference

Full reference: [Foundation: Containers](../reference/foundation-containers.md).

- [[Array]] — fixed-size array with inline storage (the engine's `std::array`)
- [[Vector]] — growable array; owns heap storage by default
- [[HeapAllocator]] — the default allocator of [[Vector]]
- [[UniquePtr]] — owns one object (move-only, optional deleter)
- [[SharedPtr]] — shares one object by reference count (atomic or plain)
- [[HashMap]] — open-addressing hash map

## Already known — seed material (verify against the code when writing)

- STL containers are not used
- Vector: growth, Reserve/Pop with assert on misuse
- HashMap: open addressing with tombstones

## Outline

### Array and Vector: which one

Engine code never writes `new T[n]` / `delete[]`; a ctest source scan (`raw_array_check`) fails the build if it
does. Instead:

- [[Array]]`<T, N>` when the size is a compile-time constant. The elements live inside the object, so an
  object holding one is as large as its other members plus the array. It is an aggregate and checks its index.
- [[Vector]]`<T>` for everything else: a size known only at runtime (`Resize`), or a size that changes
  (`Push`, `Insert`, `Remove`). It owns heap storage by default and frees it on growth and in its destructor.

```cpp compile run
Array<u32, 4> window = {{10, 20, 30, 40}};
u32 sum = 0;
for (u32 v : window) {
    sum += v;
}

Vector<u32> table; // a runtime-sized table: replaces new u32[n] / delete[]
table.Resize(3);   // elements are value-initialized, so zero
table[1] = sum;
table.Push(7);     // and it still grows

std::printf("%u %u %u %zu\n", table[0], table[1], table[3], table.Size());
```

```output
0 100 7 4
```

`Resize` aborts if the heap is exhausted; `TryResize` returns false instead, for code that reports the error
(mesh creation, material parameter buffers). `Data()` gives the raw pointer for APIs that take one; it is invalidated by
growth. A `Vector` of a type that cannot be moved (one holding an atomic) is fine as long as it is sized once with
`Resize` and never grown past that.

An arena-backed `Vector<T, ArenaAllocator>` does not free: its old blocks stay in the arena until it is reset.

### UniquePtr and SharedPtr: single objects

The same rule covers single objects: engine code never writes `new T` / `delete p` (the `raw_new_check` ctest fails
if it does). Placement `new` into storage a container owns, and the allocators in `engine/foundation/mem/`, are the only
exceptions.

- [[UniquePtr]]`<T>` owns one object and destroys it when it goes out of scope. It is move-only, converts from a
  pointer to a derived type to a pointer to its base, and takes an optional deleter. A pointer type with a custom
  deleter gets its own `using` alias so call sites do not spell the deleter: the GPU device is a
  `rhi::DevicePtr`, whose deleter waits for the GPU before destroying the device.
- [[SharedPtr]]`<T>` is for objects with several owners. The object is destroyed when the last owner goes. The default
  count is atomic, so owners on different threads may copy and release their own pointers; `LocalSharedPtr<T>`
  uses a plain count for pointers that stay on one thread. There are no weak pointers.

```cpp compile run
int alive = 0;
struct Counter {
    int* count;
    explicit Counter(int* c) : count(c) { ++*count; }
    ~Counter() { --*count; }
};

{
    UniquePtr<Counter> unique = MakeUnique<Counter>(&alive);
    UniquePtr<Counter> moved = std::move(unique); // ownership moves; `unique` is now empty
    std::printf("unique=%d moved=%d alive=%d\n", unique ? 1 : 0, moved ? 1 : 0, alive);
}

SharedPtr<Counter> first = MakeShared<Counter>(&alive);
{
    SharedPtr<Counter> second = first;
    std::printf("owners=%zu alive=%d\n", first.UseCount(), alive);
}
std::printf("owners=%zu alive=%d\n", first.UseCount(), alive);
first.Reset();
std::printf("alive=%d\n", alive);
```

```output
unique=0 moved=1 alive=1
owners=2 alive=1
owners=1 alive=1
alive=0
```

### HashMap

_TODO_

### Iteration and invalidation rules

_TODO_

### Allocation behavior and OOM policy

_TODO_

### Common mistakes

_TODO_

