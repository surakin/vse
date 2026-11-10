# ECS: entities, components and systems

Status: draft (Phase 17, Days 223-225: entities, components, storage; the rest of the page arrives with the days that build it).

The ECS (`engine/ecs`, namespace `ecs`) is the engine's data-oriented core: **entities** are ids, **components** are plain data stored in
cache-friendly chunks, and **systems** are functions over all entities that have a given set of components. Design, measurements and the
benchmark the phase must pass: `docs/superpowers/specs/2026-11-10-ecs-design.md`. The ECS includes no renderer, RHI or platform header;
the bridge to the renderer is the `engine/scene` module (Day 233).

## Entities

An `ecs::Entity` is a 64-bit id: a slot index and the slot's generation. It owns nothing and is a plain value: copy it, store it, compare it,
use it as a `HashMap` key. The all-zero value is invalid. When an entity is destroyed its slot is reused for a later entity, but with a new
generation, so an old id never names the new entity: asking whether it is alive answers no.

The `ecs::EntityTable` allocates and frees the ids and remembers where each alive entity's components are stored (its archetype, chunk and row).
A world owns one; a program normally never touches it.

```cpp compile run standalone
#include "ecs/entity_table.h"
#include "os/console.h"

using namespace engine_placeholder;

int main() {
    ecs::EntityTable table;
    const ecs::Entity first = table.Allocate(ecs::EntityLocation{0, 0, 0});
    table.Free(first);
    const ecs::Entity second = table.Allocate(ecs::EntityLocation{0, 0, 0});
    os::Print("same slot: %s\n", first.index == second.index ? "yes" : "no");
    os::Print("old id alive: %s\n", table.IsAlive(first) ? "yes" : "no");
    os::Print("new id alive: %s\n", table.IsAlive(second) ? "yes" : "no");
    return 0;
}
```

```output
same slot: yes
old id alive: no
new id alive: yes
```

### Rules and limits

- The table holds up to 4,294,967,294 slots; past that `Allocate` returns the invalid id and counts the refusal.
- A slot's generation is 32 bits and never 0. After 2^32 - 1 reuses of one slot it wraps to 1 (counted in `Stats().generation_wraps`); an id older
  than a whole wrap could match again. At one reuse per frame that takes about two years of continuous running on one slot.
- Reading (`IsAlive`, `Location` on a const table) is safe from several threads at once; `Allocate`, `Free` and a mutable `Location` need exclusive
  access (the world takes them at sync points or through its command buffer, Day 228).
- A pointer from `Location` is valid until the next `Allocate`.

## Components

A component is any plain C++ type: `struct Position { float x, y, z; };`. There is no base class and no registration call: the first use of
`ecs::ComponentOf<T>()` registers the type and returns its `ecs::ComponentId` (a `u16`), the same id on every later call and on every thread.
The registry (`ecs::ComponentRegistry::Global()`) keeps a `TypeInfo` for each type: its size and alignment, whether it is a tag, whether its
bytes can be copied to move it (`trivially_relocatable`), and the functions that move, destroy and default-construct it. Storage uses those
functions, so a component may own memory (a `Vector`) and still live in a chunk.

An empty struct is a **tag**: it marks an entity, takes no space in a chunk and still takes part in queries. A type may declare
`static constexpr const char* kComponentName = "Position";` to be named in logs; otherwise it is called `component#N`.

A `ecs::ComponentSet` is the 128-bit set of ids that identifies an archetype or a query (`Add`, `Remove`, `Has`, `IsSubsetOf`, `Intersects`,
`Hash`, and `NextFrom` to walk the ids in ascending order).

```cpp compile run standalone
#include "ecs/component.h"
#include "ecs/component_set.h"
#include "os/console.h"

using namespace engine_placeholder;

struct Position { float x, y, z; };
struct Frozen {};

int main() {
    const ecs::ComponentId position = ecs::ComponentOf<Position>();
    const ecs::ComponentId frozen = ecs::ComponentOf<Frozen>();
    const ecs::TypeInfo& info = ecs::ComponentRegistry::Global().Info(position);
    ecs::ComponentSet set;
    set.Add(position);
    set.Add(frozen);
    os::Print("Position: size %zu, tag %s\n", info.size, info.is_tag ? "yes" : "no");
    os::Print("Frozen is a tag: %s\n", ecs::ComponentRegistry::Global().Info(frozen).is_tag ? "yes" : "no");
    os::Print("set has %u ids, same id again: %s\n", set.Count(), ecs::ComponentOf<Position>() == position ? "yes" : "no");
    return 0;
}
```

```output
Position: size 12, tag no
Frozen is a tag: yes
set has 2 ids, same id again: yes
```

### Component rules and limits

- A component type must be move constructible and destructible, and not a reference, an array (wrap it in a struct), `const` or `volatile`; its
  alignment may not exceed 64 bytes. Each is a compile-time error, checked by `tests/ecs/compile_fail/`.
- A process registers up to 128 component types and an archetype holds up to 32. When the registry is full, `ComponentOf<T>()` returns
  `ecs::kInvalidComponent` for the new type (every time) and `Refused()` counts the attempts.
- The ids are small integers assigned in order of first use, so they differ between runs that touch types in a different order. Never store
  an id on disk; store the type's name.
- `ComponentOf<T>()` and `Info()` are thread-safe; registration takes a lock, once per type.

## Storage: archetypes and chunks

All entities that have exactly the same set of components form one **archetype** (`ecs::Archetype`). Its rows live in **chunks**: 32 KiB,
64-byte aligned blocks (`ecs::Chunk`, `ecs::kChunkBytes`) with a small header and one **column** per component, so a loop over one component reads
contiguous memory. Column 0 is always the entity ids. Inside a chunk the columns are ordered by descending alignment, so none needs padding, and
the number of rows a chunk holds (`Layout().capacity`) is the room after the header divided by the bytes of one row. A tag has no column.

- **No per-entity allocation.** Rows are added at the end of the archetype's last chunk; a new chunk is taken from the world's `ecs::ChunkPool`
  only when that one is full. The pool keeps released chunks up to a budget (default 64 chunks, 2 MiB) and frees the rest.
- **Removal is swap-remove.** `RemoveRow` destroys the row's components, then moves the archetype's last row into the gap, so rows stay dense
  and only the last chunk is partly filled. It returns the entity that moved, so the caller can fix that entity's location; it returns the invalid
  entity when nothing moved. An emptied last chunk goes back to the pool at once.
- **Lifetimes are exact.** `AddRow` leaves the cells uninitialized and the caller constructs each one (`CellPtr`); a component with a
  non-trivial type is moved with its `move_construct` and the source destroyed once, a trivially relocatable one with `MemCopy`. The destructor
  destroys the components still alive.
- **Pointers and spans are valid until the next add or remove** on that archetype.
- **Cost.** A row costs the sum of its components' sizes plus 8 bytes for the entity; an archetype wastes at most one partly-filled chunk
  (under 32 KiB), and a chunk header is 320 bytes. A row larger than a chunk (about 32 KB) is refused: `Valid()` is false and
  `ChunkPool::Stats().refused_layouts` counts it.
