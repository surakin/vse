# ECS: entities, components and systems

Status: draft (Phase 17, Days 223-228: entities, components, storage, the world, queries, command buffers, resources; the rest of the page arrives with the days that build it).

**Audience:** every game and engine programmer. Read [Containers](foundation-containers.md) and [Memory](foundation-memory.md) first. This page covers only
what is built so far (entities, components, storage, the world, queries, command buffers and resources); systems are not documented here yet.

The ECS (`engine/ecs`, namespace `ecs`) is the engine's data-oriented core: **entities** are ids, **components** are plain data stored in
cache-friendly chunks, and **systems** are functions over all entities that have a given set of components. Design, measurements and the
benchmark the phase must pass: `docs/superpowers/specs/2026-11-10-ecs-design.md`. The ECS includes no renderer, RHI or platform header;
the bridge to the renderer is the `engine/scene` module (Day 233).

In plain words: instead of a `Monster` object that owns its position, health and sprite, you have an id (the entity) and separate small records of data
(the components) attached to it. Entities with the same set of components are stored together, one array per component, so a loop over "every entity
with a Position" reads memory in order. The *archetype* is that group: all entities with exactly the same component set.

## At a glance

| I need | Use | Notes |
|---|---|---|
| a thing in the world | `world.Create(components...)` returns an `ecs::Entity` | a plain 64-bit value; copy and compare freely |
| to ask whether an entity still exists | `world.Exists(e)` | a destroyed entity's id never matches a later one |
| to give an entity a new component | `world.Add<T>(e, value)` | returns `ecs::Status`; moves the entity's row |
| to read or change a component | `world.Get<T>(e)` | pointer valid until the next structural change; null for a tag |
| to mark an entity without data | an empty struct (a tag) | takes no space but takes part in queries |
| to visit every entity that has certain components | `world.Query<Write<A>, Read<B>, Without<C>>()`, then `ForEach` | see "Queries"; include `ecs/query.h` |
| to change entities while a query runs | `CommandBuffer`, then `world.Apply(buffer)` | see "Command buffers"; include `ecs/command_buffer.h` |
| one value shared by all systems (time, settings) | `world.SetResource(value)`, `world.Resource<T>()` | see "Resources" |
| to check the world's consistency | `world.Validate()` | for tests and debug builds |

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

## The world

An `ecs::World` owns the entities, the archetypes and the chunks. Everything that changes which components an entity has is a **structural
change**: `Create`, `Destroy`, `Add<T>`, `Remove<T>`, and `Set<T>` when it has to add the component.

```cpp compile run standalone
#include "ecs/world.h"
#include "os/console.h"

using namespace engine_placeholder;

struct Position { float x, y; };
struct Frozen {};

int main() {
    ecs::World world;
    const ecs::Entity e = world.Create(Position{1, 2});
    world.Add<Frozen>(e, {});
    world.Get<Position>(e)->x = 5;
    os::Print("x %.0f, frozen %s\n", static_cast<double>(world.Get<Position>(e)->x), world.Has<Frozen>(e) ? "yes" : "no");
    world.Remove<Frozen>(e);
    world.Destroy(e);
    os::Print("alive %s, destroy again: %s\n", world.Exists(e) ? "yes" : "no",
              world.Destroy(e) == ecs::Status::DeadEntity ? "DeadEntity" : "other");
    return world.Validate().Ok() ? 0 : 1;
}
```

```output
x 5, frozen yes
alive no, destroy again: DeadEntity
```

- **Statuses.** Calls on one entity return `ecs::Status`: `Ok`, `DeadEntity` (stale or invalid id), `MissingComponent` (`Remove`), `AlreadyHas` (`Add`;
  `Set` overwrites instead) or `Refused` (a limit: more than 32 components, 128 types, no memory; counted in `Stats().refused`).
- **Migration.** `Add` and `Remove` move the entity's row to the archetype of the new component set: the components both have are moved over, a
  dropped one is destroyed, the new one is constructed, and the row swapped into the hole keeps its entity table entry correct. The edge
  (archetype, component, add or remove) to the target is cached, so a repeated transition costs one hash lookup (`Stats().graph_hits`).
- **Pointer validity.** A pointer from `Get<T>` or a chunk span is valid until the next structural change; `Get` of a tag is null (ask `Has`).
- **Iteration guard.** While `BeginIteration` is active (queries do this), a structural call fails an `ENGINE_ASSERT` naming the call;
  `Set` of an existing component is allowed. A command buffer defers the changes to a sync point.
- **Cost.** `Create` is one entity-table allocation and one row; `Add`/`Remove` copy the row once (the size of the entity's components) plus one
  hash lookup; `Destroy` destroys the row and moves the last one in; `Get` is two index loads.
- **Validation.** `Validate()` checks the invariants (locations point at their rows, no dead or duplicated entity in a row, counts add up, chunks
  full except the last) and is meant for tests and debug builds.

## Queries

A **query** answers "give me every entity that has these components". You describe the components with **terms**, and the query calls your function once
per matching entity with exactly the data you asked for. Include `ecs/query.h` and make one from the world:

```cpp compile run standalone
#include "ecs/query.h"
#include "os/console.h"

using namespace engine_placeholder;

struct Position { float x, y; };
struct Velocity { float x, y; };
struct Frozen {};

int main() {
    ecs::World world;
    world.Create(Position{0, 0}, Velocity{1, 2});
    world.Create(Position{10, 0}, Velocity{1, 1}, Frozen{});  // frozen: the query below skips it
    world.Create(Position{20, 0});                            // no Velocity: skipped

    auto movers = world.Query<ecs::Write<Position>, ecs::Read<Velocity>, ecs::Without<Frozen>>();
    movers.ForEach([](Position& p, const Velocity& v) {
        p.x += v.x;
        p.y += v.y;
    });

    auto positions = world.Query<ecs::Read<Position>>();
    positions.ForEach([](ecs::Entity, const Position& p) { os::Print("(%.0f, %.0f)\n", static_cast<double>(p.x), static_cast<double>(p.y)); });
    os::Print("%u of %u entities with a position moved\n", movers.Count(), positions.Count());
    return 0;
}
```

```output
(1, 2)
(10, 0)
(20, 0)
1 of 3 entities with a position moved
```

### The terms

| Term | The entity must | The function gets |
|---|---|---|
| `Read<T>` | have `T` | `const T&` |
| `Write<T>` | have `T`; the query holds `T` for writing | `T&` |
| `Optional<T>` | may have `T` | `const T*`, null when it lacks `T` |
| `OptionalWrite<T>` | may have `T` | `T*`, null when it lacks `T` |
| `With<T>` | have `T` | nothing (a filter; works for tags) |
| `Without<T>` | not have `T` | nothing (a filter; works for tags) |

A query has one to 16 terms and names each component once, so `Read<T>` with `Write<T>`, or `With<T>` with `Without<T>`, is a **compile error**, as is
`Read`, `Write` or `Optional` of a tag (a tag has no data: use `With`). The function takes the data terms in order, and may take an `ecs::Entity` first.

### How it works

- **The match is cached.** A query remembers the archetypes that match and how many archetypes the world had when it last looked. Each use checks only the
  archetypes created since, so a query kept across frames costs almost nothing to refresh. Keep queries; do not rebuild them every frame.
- **Empty archetypes are skipped, not removed.** `MatchedArchetypes()` counts them; `Count()` and `IsEmpty()` count entities.
- **The order is stable:** archetypes in creation order, then chunks, then rows. Two worlds built the same way are visited in the same order.
- **No allocation** while iterating: the cost is the loop and one lookup of each column per chunk.
- **`ForEachChunk`** gives the function a `ChunkView` instead of one entity at a time: `Count()`, `Entities()`, `Write<T>()` and `Read<T>()` as spans. Use it
  for loops the compiler can vectorize, or to split a chunk's work. For an optional column the span is empty when the archetype lacks it.
- **Stopping early.** If your function returns `bool`, returning `false` ends the iteration.
- **The iteration guard.** While a query runs, a structural call (`Create`, `Destroy`, `Add`, `Remove`) fails an assertion, and `world.Get<T>` of a component the
  query *writes* fails one too. `world.Get<T>` of a component the query only reads is fine, and so is `Set` of an existing component.

## Command buffers

A structural change (create, destroy, add or remove a component) moves rows, so it is not allowed while a query runs. A **command buffer** records the
changes and you apply them afterwards, at a **sync point**. Include `ecs/command_buffer.h`.

```cpp compile run standalone
#include "ecs/command_buffer.h"
#include "ecs/query.h"
#include "os/console.h"

using namespace engine_placeholder;

struct Health { unsigned hp; };
struct Burning {};

int main() {
    ecs::World world;
    for (unsigned i = 0; i < 6; ++i) {
        world.Create(Health{i * 10});
    }

    ecs::CommandBuffer buffer;
    auto healths = world.Query<ecs::Read<Health>>();
    healths.ForEach([&](ecs::Entity e, const Health& h) {
        if (h.hp == 0) {
            buffer.Destroy(e);                 // recorded, not done: the loop is still running
        } else if (h.hp >= 40) {
            buffer.Add<Burning>(e, {});
        }
    });
    const ecs::Entity spawned = buffer.Create(Health{99});  // a placeholder for an entity that does not exist yet
    buffer.Add<Burning>(spawned, {});                       // later commands may name it

    const ecs::PlaybackReport report = world.Apply(buffer);
    os::Print("applied %u, ignored %u, created %u\n", report.applied, report.ignored, report.created);
    os::Print("%u entities, the spawned one is real: %s\n", healths.Count(), world.Exists(buffer.Resolve(spawned)) ? "yes" : "no");
    return 0;
}
```

```output
applied 5, ignored 0, created 1
6 entities, the spawned one is real: yes
```

- **Placeholders.** `Create` returns an entity with a reserved generation (`Entity::IsPlaceholder()`). It is valid only with the buffer that made it; after
  `Apply`, `buffer.Resolve(placeholder)` returns the real entity (invalid if the create did not happen).
- **Values** are moved into the buffer when you record and moved out when it is applied. A command never applied (the buffer is cleared or destroyed)
  destroys its values exactly once.
- **Skipped commands.** A command whose entity died, or that cannot apply (adding a component the entity has), is skipped and counted in
  `PlaybackReport::ignored`; playback never stops at a failure.
- **Order.** Commands apply in the order recorded. For several buffers, apply them in an order you choose; `CommandBufferPool::ApplyAll` applies by index,
  so with one buffer per parallel job the result does not depend on which thread ran which job.
- **Cost.** Recording appends to storage the buffer keeps between uses, so a buffer that is cleared and reused allocates nothing after the first frames.
  Applying during an iteration fails an assertion.

## Resources

A **resource** is one value of a type that belongs to the world as a whole: the frame time, the input state, a settings snapshot. There is at most one per type.

```cpp compile run standalone
#include "ecs/world.h"
#include "os/console.h"

using namespace engine_placeholder;

struct Time { double delta; unsigned frame; };

int main() {
    ecs::World world;
    world.SetResource(Time{0.016, 0});
    world.Resource<Time>().frame += 1;                         // read or change it in place
    os::Print("frame %u, delta %.3f\n", world.Resource<Time>().frame, world.Resource<Time>().delta);
    os::Print("has Time: %s, has int: %s\n", world.HasResource<Time>() ? "yes" : "no", world.TryResource<int>() != nullptr ? "yes" : "no");
    return 0;
}
```

```output
frame 1, delta 0.016
has Time: yes, has int: no
```

`SetResource` of a type that already has one replaces the value in place, so a reference you hold stays valid. `Resource<T>()` of a missing type fails an
assertion; `TryResource<T>()` returns null instead. The world destroys its resources newest first when it goes away, so a resource may refer to one made
before it.

## Common mistakes

- **Keeping a pointer from `Get<T>` across an `Add`, `Remove`, `Create` or `Destroy`.** The row may have moved; fetch the pointer again.
- **Ignoring the returned `ecs::Status`.** `DeadEntity`, `AlreadyHas` and `Refused` are normal answers, not crashes.
- **Changing components while iterating.** A structural call during iteration fails an `ENGINE_ASSERT`; record it in a `CommandBuffer` and apply it after the loop.
- **Using a placeholder with another buffer or after `Clear`.** It resolves to nothing; resolve it right after `Apply`.
- **Writing a component through `Get` inside a query that writes it.** Use the argument the query gives you.
- **Building a query every frame.** A query keeps its match; make it once and keep it.
- **Storing a `ComponentId` on disk.** Ids depend on the order of first use; store the type's name.
- **Using an `Entity` as if it owned the entity.** It is only an id; destroying it is the world's job.

## See also

[Containers](foundation-containers.md), [Jobs](jobs.md) (the systems will run on them), `docs/superpowers/specs/2026-11-10-ecs-design.md`.
