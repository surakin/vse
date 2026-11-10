# Foundation: Containers

Status: written (Phases 0 and 10).

**Audience:** every engine programmer. Read [Standard types](foundation-types.md) (the vocabulary) and [Memory](foundation-memory.md) (what an allocator is) first.

The engine has its own containers and never uses the standard library's. They are small, predictable, and take an allocator you choose. This page
teaches the ones you will use every day: `Array`, `Vector`, `HashMap`, `UniquePtr`, `SharedPtr` and `Span`. The table "Which type do I use?" in
[Standard types](foundation-types.md) says which to reach for; the `std_usage_check` test enforces it.

## At a glance

| I need | Use | Notes |
|---|---|---|
| a fixed number of items, known at compile time | `Array<T, N>` | lives inside the object; no allocation |
| a list that grows and shrinks | `Vector<T>` | heap by default; `Vector<T, ArenaAllocator>` for frame data |
| find a value by key | `HashMap<K, V, Allocator>` | no iteration; needs `Hash<K>` and `==` |
| one object with one owner | `UniquePtr<T>` | `MakeUnique<T>(args...)` |
| one object with several owners | `SharedPtr<T>` | `MakeShared<T>(args...)`; reference counted |
| look at a run of items without owning them | `Span<T>` | pointer + length, bounds checked |

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
- [[Span]] — a non-owning view of consecutive elements (`ByteSpan` for bytes), see [Standard types](foundation-types.md)

### Sorting, searching and copying spans

`algorithm.h` holds the small algorithms the engine uses instead of `<algorithm>`: `Min`, `Max`, `Clamp`, `IndexOf(span, value)`,
`Sort(span, less)` (in place, no allocation, not stable) and `StableSort(span, less)` (keeps equal elements in order; allocates a
scratch buffer). `Append(vector, span)` and `Assign(vector, span)` copy a span into a vector, growing it once.

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

[[HashMap]]`<K, V, Allocator>` stores `V` values under `K` keys by open addressing with linear probing. You give it the allocator that holds its table;
nothing is allocated from the heap behind your back.

```cpp compile run standalone
#include "containers/hash_map.h"
#include "mem/heap_allocator.h"
#include "os/console.h"

using namespace engine_placeholder;

int main() {
    HashMap<u32, f32, HeapAllocator> health(HeapAllocator::Default());
    health.Insert(7, 100.0f);          // add, or replace if the key exists
    health.Insert(9, 55.5f);
    health.Insert(7, 80.0f);           // replaces 100

    if (f32* hp = health.Find(7)) {    // a pointer to the stored value, or nullptr
        *hp -= 30.0f;                  // edit in place
    }
    os::Print("7 -> %.1f, 9 -> %.1f, 4 -> %s\n", static_cast<f64>(*health.Find(7)), static_cast<f64>(*health.Find(9)),
              health.Find(4) == nullptr ? "absent" : "present");

    health.Remove(9);
    os::Print("size %zu, removed key is %s\n", health.Size(), health.Find(9) == nullptr ? "gone" : "still there");
    return 0;
}
```

```output
7 -> 50.0, 9 -> 55.5, 4 -> absent
size 1, removed key is gone
```

To use your own type as a key give it `operator==` and specialize `Hash<K>` (see `Hash<ecs::Entity>` in `engine/ecs/entity.h` for a short example);
the key and the value must be default-constructible, and both are stored by value. There is **no iteration**: a map answers "what is stored for this
key?", not "what is in it?". If you need to walk the items keep them in a `Vector` and use the map as an index into it.

### Iteration and invalidation rules

Most container bugs are a pointer, reference or span that outlived a change. The rules are short:

| Operation | What stops being valid |
|---|---|
| `Vector::Push`, `Insert`, `Resize`, `Reserve` that grows | every pointer, reference, iterator and span into the vector |
| `Vector::Remove`, `Pop`, `Clear` | pointers to the removed elements, and to those after a removed one |
| `HashMap::Insert` | every pointer from `Find` (the table may rehash) |
| `HashMap::Remove` | the pointer to the removed value |
| moving a `Vector` into another | nothing is copied: the new vector owns the same storage, the old one is empty |
| `ArenaAllocator::Reset` | every container that drew from the arena |

Reading is safe from several threads at once; any write needs exclusive access. None of the containers does its own locking. To remove items while
looping, go backwards by index, or collect what to remove first and remove afterwards.

```cpp compile run
Vector<u32> numbers;
for (u32 i = 0; i < 10; ++i) {
    numbers.Push(i);
}
// Remove the even numbers: iterate from the back so an index never skips an element.
for (usize i = numbers.Size(); i > 0; --i) {
    if (numbers[i - 1] % 2 == 0) {
        numbers.Remove(i - 1);
    }
}
std::printf("%zu left, first %u, last %u\n", numbers.Size(), numbers[0], numbers[numbers.Size() - 1]);
```

```output
5 left, first 1, last 9
```

### Allocation behavior and OOM policy

Containers do not throw (exceptions are off). When the allocator runs out of memory:

- `Push`, `Insert`, `Reserve`, `Resize` **abort** with a message naming the container (a precondition the program cannot continue without).
- `TryReserve` and `TryResize` return `false` and change nothing: use these where running out is a real possibility and you report an error to the
  caller (loading a large asset, building a mesh).
- `HashMap` aborts if it cannot allocate its table.
- `Alloc` itself (the allocator) returns `nullptr`; only the code that called it can decide what that means.

`operator[]` on a `Vector` or `HashMap::Find` result is **not** bounds-checked in release builds: an out-of-range index is a bug in your code. `Span`
and `Array` do check. Prefer passing a `Span` to functions that read a sequence: it tells the reader that nothing is taken or stored.

### Common mistakes

- **Keeping a pointer into a `Vector` across a `Push`.** The vector may have moved its storage. Keep an index instead.
- **Holding the result of `HashMap::Find` across an `Insert`.** Find again.
- **Declaring a container before its allocator.** The allocator must outlive the container; declare the allocator first.
- **Using `new`/`delete` or the standard containers.** The gates fail the build; use the types above.
- **Copying.** `Vector`, `HashMap` and the smart pointers cannot be copied; this is deliberate (copies of big data should be visible). Move them with
  `Move(x)`, or copy elements with `Append(vector, span)`.
- **Storing a `Span` or a `StringView` in a long-lived object** whose source may be freed. A view is only as long-lived as the thing it looks at.
- **A `SharedPtr` where a `UniquePtr` would do.** Shared ownership makes lifetimes hard to see; start with one owner.

### See also

[Memory](foundation-memory.md), [Strings](foundation-strings.md), [Standard types](foundation-types.md); reference:
[Foundation: Containers](../reference/foundation-containers.md).
