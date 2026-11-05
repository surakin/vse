# Foundation: Containers

> **Status:** skeleton — not yet written  
> **Audience:** engine programmers  
> **Covers:** Phase 0

## Source of truth

- `engine/foundation/containers/array.h`
- `engine/foundation/containers/vector.h`
- `engine/foundation/mem/heap_allocator.h`
- `engine/foundation/containers/hash_map.h`

## API reference

Full reference: [Foundation: Containers](../reference/foundation-containers.md).

- [[Array]] — fixed-size array with inline storage (the engine's `std::array`)
- [[Vector]] — growable array; owns heap storage by default
- [[HeapAllocator]] — the default allocator of [[Vector]]
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

### HashMap

_TODO_

### Iteration and invalidation rules

_TODO_

### Allocation behavior and OOM policy

_TODO_

### Common mistakes

_TODO_

