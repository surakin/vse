# ECS: entities, components and systems

Status: written. Not part of this module: serialization (see [storage](storage.md)) and events.

**Audience:** every game and engine programmer. Read [Containers](foundation-containers.md) and [Memory](foundation-memory.md) first.

The ECS (`engine/ecs`, namespace `ecs`) is the engine's data-oriented core: **entities** are ids, **components** are plain data stored in
cache-friendly chunks, and **systems** are functions over all entities that have a given set of components. Design, measurements and the
benchmark it must pass: `docs/superpowers/specs/2026-11-10-ecs-design.md`. The ECS includes no renderer, RHI or platform header;
the bridge to the renderer is the `engine/scene` module.

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
| to visit only what changed | `Changed<T>` / `Added<T>` terms, then `query.Finish()` | see "Change detection" |
| to run logic every frame in a defined order | `ecs::Schedule`, `MakeQuerySystem`, `Build()`, `Run(world, dt)` | see "Systems and schedules"; include `ecs/schedule.h` |
| one value shared by all systems (time, settings) | `world.SetResource(value)`, `world.Resource<T>()` | see "Resources" |
| a parent-child transform hierarchy | `CreateTransformEntity`, `SetParent`, `MakeTransformSystem` | see "Transforms and hierarchy"; include `ecs/transform.h` |
| entities drawn by the renderer | `scene::MeshRenderer`, `LightSource`, `Terrain`, `MakeRenderSyncSystem`, `MakeTerrainSyncSystem` | see "Rendering"; include `scene/render_sync.h` |
| to react to a component being removed | `kTrackRemoval`, `world.RemovedCount<T>()` / `RemovedAt<T>(i)` | see "Removal tracking" |
| to check the world's consistency | `world.Validate()` | for tests and debug builds |

## Entities

An `ecs::Entity` is a 64-bit id: a slot index and the slot's generation. It owns nothing and is a plain value: copy it, store it, compare it,
use it as a `HashMap` key. The all-zero value is invalid. When an entity is destroyed its slot is reused for a later entity, but with a new
generation, so an old id never names the new entity: asking whether it is alive answers no.

The `ecs::EntityTable` allocates and frees the ids and remembers where each alive entity's components are stored (its archetype, chunk and row).
A world owns one; a program normally never touches it.

```cpp compile run standalone
#include "ecs/entity_table.h"
#include "log/output.h"

using namespace vse;

int main() {
    ecs::EntityTable table;
    const ecs::Entity first = table.Allocate(ecs::EntityLocation{0, 0, 0});
    table.Free(first);
    const ecs::Entity second = table.Allocate(ecs::EntityLocation{0, 0, 0});
    out::Print("same slot: %s\n", first.index == second.index ? "yes" : "no");
    out::Print("old id alive: %s\n", table.IsAlive(first) ? "yes" : "no");
    out::Print("new id alive: %s\n", table.IsAlive(second) ? "yes" : "no");
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
  access (the world takes them at sync points or through its command buffer).
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
#include "log/output.h"

using namespace vse;

struct Position { float x, y, z; };
struct Frozen {};

int main() {
    const ecs::ComponentId position = ecs::ComponentOf<Position>();
    const ecs::ComponentId frozen = ecs::ComponentOf<Frozen>();
    const ecs::TypeInfo& info = ecs::ComponentRegistry::Global().Info(position);
    ecs::ComponentSet set;
    set.Add(position);
    set.Add(frozen);
    out::Print("Position: size %zu, tag %s\n", info.size, info.is_tag ? "yes" : "no");
    out::Print("Frozen is a tag: %s\n", ecs::ComponentRegistry::Global().Info(frozen).is_tag ? "yes" : "no");
    out::Print("set has %u ids, same id again: %s\n", set.Count(), ecs::ComponentOf<Position>() == position ? "yes" : "no");
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
#include "log/output.h"

using namespace vse;

struct Position { float x, y; };
struct Frozen {};

int main() {
    ecs::World world;
    const ecs::Entity e = world.Create(Position{1, 2});
    world.Add<Frozen>(e, {});
    world.Get<Position>(e)->x = 5;
    out::Print("x %.0f, frozen %s\n", static_cast<double>(world.Get<Position>(e)->x), world.Has<Frozen>(e) ? "yes" : "no");
    world.Remove<Frozen>(e);
    world.Destroy(e);
    out::Print("alive %s, destroy again: %s\n", world.Exists(e) ? "yes" : "no",
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
#include "log/output.h"

using namespace vse;

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
    positions.ForEach([](ecs::Entity, const Position& p) { out::Print("(%.0f, %.0f)\n", static_cast<double>(p.x), static_cast<double>(p.y)); });
    out::Print("%u of %u entities with a position moved\n", movers.Count(), positions.Count());
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
#include "log/output.h"

using namespace vse;

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
    out::Print("applied %u, ignored %u, created %u\n", report.applied, report.ignored, report.created);
    out::Print("%u entities, the spawned one is real: %s\n", healths.Count(), world.Exists(buffer.Resolve(spawned)) ? "yes" : "no");
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
#include "log/output.h"

using namespace vse;

struct Time { double delta; unsigned frame; };

int main() {
    ecs::World world;
    world.SetResource(Time{0.016, 0});
    world.Resource<Time>().frame += 1;                         // read or change it in place
    out::Print("frame %u, delta %.3f\n", world.Resource<Time>().frame, world.Resource<Time>().delta);
    out::Print("has Time: %s, has int: %s\n", world.HasResource<Time>() ? "yes" : "no", world.TryResource<int>() != nullptr ? "yes" : "no");
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

## Change detection

Many systems only need the data that changed since they last ran: the renderer sync re-uploads moved transforms, the hierarchy updates children of moved
parents. Add a **`Changed<T>`** or **`Added<T>`** term and the query skips chunks that did not change.

**Component versions** answer a coarser question in constant time: "may anything about `T` have changed in this world since I last looked?" Call
`world.TrackComponentVersion<T>()` once; from then on `world.ComponentVersion<T>()` grows whenever an entity gains or loses `T` (`Add`, `Remove`, `Create` with it,
`Destroy`) or a value of `T` may have been written (`Set`, the non-const `Get`, `Poke`, `Stamp`, the non-const `CellOf`, and a query that declares a write of `T`, counted
once when it begins). It can grow without a real change, never the reverse, so a cache derived from `T` compares two readings and rebuilds when they differ. An
untracked type costs one bit test on those paths and reads as 0. The hierarchy's children index is the first user. `world.StorageOrder(entity)` gives the order a query
visits entities in now (archetype, chunk, row), for a caller that must reproduce it without running the query.

```cpp compile run standalone
#include "ecs/query.h"
#include "log/output.h"

using namespace vse;

struct Position { float x, y; };

int main() {
    ecs::World world;
    ecs::Entity first = world.Create(Position{0, 0});
    world.Create(Position{5, 5});

    auto moved = world.Query<ecs::Changed<Position>, ecs::Read<Position>>();
    unsigned seen = 0;
    moved.ForEach([&](const Position&) { ++seen; });
    out::Print("first pass: %u\n", seen);            // a creation counts as a change

    moved.Finish();                                  // "I have handled everything so far"
    seen = 0;
    moved.ForEach([&](const Position&) { ++seen; });
    out::Print("second pass: %u\n", seen);           // nothing new

    world.Set<Position>(first, Position{1, 1});      // a write after Finish
    seen = 0;
    moved.ForEach([&](const Position&) { ++seen; });
    out::Print("after a write: %u\n", seen);
    return 0;
}
```

```output
first pass: 2
second pass: 0
after a write: 2
```

The last line is the point to understand: the precision is **one chunk**. Writing one entity marks its whole chunk, so both entities (they share a chunk) are
visited. A query is told about chunks, not rows; if you need exactness, store a flag or compare values in the function.

- **What counts as a write.** A `Write` or `OptionalWrite` term marks the chunk's column when the chunk is visited (whether or not the function changes
  anything), `Set` of an existing component, the non-const `world.Get<T>` (the pointer may be written through), and a row arriving in a chunk (a created
  entity, or one migrated in by `Add`/`Remove`) mark the column. A `Read` pass and the const `Get` mark nothing.
- **`Added<T>`** passes chunks that *received a row with `T`*: new entities, entities that gained `T`, and entities that migrated into the archetype. It
  does not pass on a plain write.
- **Ticks.** The world has a 64-bit tick (`world.Tick()`, starting at 1). A column remembers the tick of its last write and last addition; a query remembers
  `Since()`, and a filter passes a chunk stamped later than that. `query.Finish()` sets `Since()` to the current tick and then advances the world's tick,
  so a write made afterwards (even by a system that runs later in the same frame) is newer and is seen by the next pass, while the query's own writes are not. Call it right after the pass, when no other job is writing.
  `SetSince` sets it by hand (tests). The 64-bit tick does not wrap in practice.
- **Terms.** `Changed<T>` and `Added<T>` require `T`, hand the function nothing, may sit beside `Read<T>`/`Write<T>` of the same component, and combine with
  every other term. All filters of a query must pass. `Count()` ignores them (it counts matching entities).
- **Cost.** Marking is one 8-byte store per written column per chunk visited (a chunk of 4,000 rows costs one store, not 4,000), and a filter is one load and
  compare per chunk. The measured overhead on the 1M-entity benchmark is recorded under "Measured cost" below.

## Systems and schedules

A **system** is a function that runs once per frame over the world. A **schedule** holds the systems, puts them in order and runs a frame. Include
`ecs/schedule.h`.

```cpp compile run standalone
#include "ecs/schedule.h"
#include "log/output.h"

using namespace vse;

struct Position { float x, y; };
struct Velocity { float x, y; };
struct Lifetime { unsigned frames_left; };

int main() {
    ecs::World world;
    for (unsigned i = 0; i < 4; ++i) {
        world.Create(Position{0, 0}, Velocity{1, 0}, Lifetime{i});
    }

    ecs::Schedule schedule;
    // The access (reads Velocity, writes Position) is deduced from the query's terms.
    schedule.Add(ecs::MakeQuerySystem<ecs::Write<Position>, ecs::Read<Velocity>>(
        "move", [](ecs::SystemContext& context, ecs::Query<ecs::Write<Position>, ecs::Read<Velocity>>& query) {
            const float dt = context.Dt();
            query.ForEach([dt](Position& p, const Velocity& v) { p.x += v.x * dt; });
        }));
    // A system cannot destroy entities while it iterates: it records the change, which applies at the end of the stage.
    schedule.Add(ecs::MakeQuerySystem<ecs::Write<Lifetime>>(
                     "expire",
                     [](ecs::SystemContext& context, ecs::Query<ecs::Write<Lifetime>>& query) {
                         query.ForEach([&](ecs::Entity e, Lifetime& l) {
                             if (l.frames_left == 0) {
                                 context.Commands().Destroy(e);
                             } else {
                                 --l.frames_left;
                             }
                         });
                     })
                     .After("move"));

    const ecs::BuildReport report = schedule.Build();
    out::Print("built: %s\n", report.ok ? "yes" : report.message);
    for (int frame = 0; frame < 3; ++frame) {
        schedule.Run(world, 0.5f);
        out::Print("frame %d: %u entities\n", frame, world.Query<ecs::Read<Position>>().Count());
    }
    return 0;
}
```

```output
built: yes
frame 0: 3 entities
frame 1: 2 entities
frame 2: 1 entities
```

- **Making a system.** `MakeQuerySystem<Terms...>(name, fn)` makes a system whose function gets the `SystemContext` and a query over the terms (the query is made
  once and kept). `MakeSystem(name, fn)` makes one with only the context; declare what it touches with `Reads<...>()`, `Writes<...>()`,
  `ReadsResource<...>()` and `WritesResource<...>()`. The name is a string literal.
- **Order.** `InStage(stage)` picks the stage (`kPreUpdate`, `kUpdate` (the default), `kPostUpdate`, `kRender`; `schedule.AddStage(name, after)` makes more).
  Within a stage, `After("name")` and `Before("name")` order systems; everything else runs in registration order. `Build()` makes the order once and
  returns a report: a cycle is rejected with its path, an unknown or duplicated name and a constraint that goes against the stage order are rejected too.
- **The context.** `Dt()`, `Frame()`, `Commands()` (the system's own command buffer), `Query<...>()` for extra queries, `Resource<T>()` (needs
  `WritesResource`) and `ReadResource<T>()`. The context has no structural calls: record them in `Commands()`.
- **Commands.** After each stage the schedule applies its systems' command buffers in **registration order** (not run order), so the result does not depend on
  constraints that exist only for data. A later system's command on an entity an earlier one destroyed is skipped and counted
  (`Stats().commands_ignored`). At the end of the frame the schedule calls `World::Advance()`.
- **Declared access.** A query or resource the system did not declare is reported (`ENGINE_ENSURE`) and counted in `Stats().access_violations`. Today it is a
  check; the parallel scheduler relies on it to decide which systems may run at once.
- **Run condition.** `RunIf(fn)` with a `bool(SystemContext&)` function skips the system in frames where it returns false.
- **Statistics and settings.** With the setting `ecs.stats` (environment `ENGINE_ECS_STATS`, flag `--ecs-stats`; or `schedule.SetStatsEnabled(true)`), each
  system's run time, entities visited and chunks skipped by `Changed`/`Added` are in `schedule.StatsOf("name")`. `RegisterEcsSettings` declares
  `ecs.stats` and `ecs.parallel_systems`; `UseSettings(registry)` reads them at the start of each frame.
- **Cost.** A system function up to 64 bytes is stored inline; a bigger one is on the heap (`Schedule::HeapFallbacks()`). With statistics off a system costs
  one predictable branch.

## Parallel systems

Give the schedule a job scheduler and systems that do not conflict run at the same time:

```cpp
jobs::Scheduler jobs(jobs::Scheduler::Config{});   // the frame thread is participant 0
schedule.SetScheduler(&jobs);
schedule.Build();
schedule.Run(world, dt);                           // call it on the frame thread, outside any job
```

- **Waves.** `Build()` puts the systems of each stage into **waves**. Two systems conflict when one writes a component or resource the other reads or writes
  (what each declared, or what `MakeQuerySystem` deduced). A system goes into the wave after the latest earlier system that conflicts with it or must precede
  it (`After`/`Before`), so it never overtakes one; systems in one wave are independent and run as jobs, and the frame thread helps until the wave is done.
  `schedule.WaveOf(position)` shows the result. Commands still apply after the stage in registration order, so structural changes do not depend on timing.
- **Declare the truth.** The waves are only as good as the declarations. A query or resource a system touches without declaring it is counted in
  `Stats().access_violations` (and reported once); a system that lies can race with its wave mates. Run the tests under ThreadSanitizer.
- **Splitting one system.** A system that is alone in its wave has the frame thread to itself and may split its chunks over the workers:
  `context.ForEachChunkParallel(query, fn)` calls `fn(ChunkView&, CommandBuffer&)` per chunk (a chunk is never shared by two jobs), and each range of chunks
  records into its own buffer, applied in chunk order after the system. `context.ReduceChunks<T>(query, init, per_chunk, combine)` folds one value per chunk
  in chunk order, so even a floating-point sum is the same at every worker count. In a wave with other systems the same calls run on one thread, in the same
  order, with the same result.
- **Off switch.** With `ecs.parallel_systems` off, or with no scheduler, every system runs on the calling thread in the sequential order.
- **Determinism contract.** For systems whose result does not depend on the order entities are visited, a frame leaves the world in the state the sequential
  run does, bit for bit, at every worker count. Order-sensitive reductions use `ReduceChunks`. A system that depends on the visiting order of one chunk's rows
  still sees them in row order; one that depends on the order of chunks should use `ReduceChunks` or `ForEachChunk`.
- **What a job may not do.** Call structural functions (the world is in a parallel phase and refuses them), wait for other jobs (a job cannot `HelpUntil`),
  or touch undeclared data. `context.Query<>()` inside a job is fine for declared components.

## Transforms and hierarchy

Include `ecs/transform.h`. An entity with a place in the world carries a **`LocalTransform`** (position, rotation, scale relative to its parent) and a
**`WorldTransform`** (the 4x4 matrix the renderer reads). A child names its parent with **`Parent`** and carries a **`HierarchyDepth`** (0 for a root, the
parent's depth plus one for a child); use `CreateTransformEntity`, `SetParent` and `ClearParent` and the depth is kept for you.

```cpp compile run standalone
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "log/output.h"

using namespace vse;

int main() {
    ecs::World world;
    world.SetResource(ecs::TransformReport{});

    ecs::LocalTransform place;
    place.position = Vec3(10.0f, 0.0f, 0.0f);
    const ecs::Entity ship = ecs::CreateTransformEntity(world, place);
    place.position = Vec3(0.0f, 2.0f, 0.0f);
    const ecs::Entity turret = ecs::CreateTransformEntity(world, place);
    ecs::SetParent(world, turret, ship);

    ecs::Schedule schedule;
    schedule.Add(ecs::MakeTransformSystem("transforms"));
    schedule.Build();
    schedule.Run(world, 0.016f);
    ecs::WorldTransform* turret_world = world.Get<ecs::WorldTransform>(turret);
    out::Print("turret at %.0f %.0f\n", static_cast<double>(turret_world->matrix.col[3].x), static_cast<double>(turret_world->matrix.col[3].y));

    world.Get<ecs::LocalTransform>(ship)->position = Vec3(20.0f, 0.0f, 0.0f);   // move the parent; the child follows
    schedule.Run(world, 0.016f);
    turret_world = world.Get<ecs::WorldTransform>(turret);
    out::Print("turret at %.0f %.0f\n", static_cast<double>(turret_world->matrix.col[3].x), static_cast<double>(turret_world->matrix.col[3].y));
    return 0;
}
```

```output
turret at 10 2
turret at 20 2
```

- **The system.** `MakeTransformSystem(name)` computes `WorldTransform = parent's WorldTransform * LocalTransform`. Roots go first, then children by depth, shallowest
  first; each depth level runs in parallel when the system is alone in its wave. It needs a `TransformReport` resource (`SetResource(TransformReport{})`), which counts
  frames, recomputed and written matrices, and what it repaired or refused.
- **Only real changes are written.** A matrix is stored only when it differs from the old one, and only then is its chunk stamped, so `Changed<WorldTransform>`
  (the renderer's, the spatial index's) sees exactly the moved subtrees. A frame in which nothing moved marks nothing.
- **Limits.** The depth is at most `kMaxHierarchyDepth` (64). `SetParent` returns `HierarchyStatus::Cycle`, `SelfParent`, `DeadEntity` or `TooDeep` and changes nothing
  (a refusal for depth is counted in `TransformReport::refused_depth`). Reparenting moves the whole subtree, whose depths are rebased.
- **Bad data survives.** A parent destroyed behind a child's back makes the child a root (`dangling_fixed`); a stored depth that is wrong is recomputed from the chain
  (`depth_repaired`); an entity whose chain loops keeps its previous matrix (`unresolved`). Writing `Parent` directly with `world.Set` works but is repaired a frame late.
- **Destroying a subtree.** `DestroyRecursive(world, commands, root)` records the destruction of the root and every descendant into a command buffer and returns how many,
  deepest first and, within a depth, in the order a query over `Parent` visits them.
- **Cost** (query and hierarchy indices series). `SetParent`, `ClearParent` and `DestroyRecursive` keep a children index beside the hierarchy (a world resource),
  so a call costs the child's subtree: about 200 ns per `SetParent` when building a wide tree of 1,000 or 4,000 entities (5 and 19 µs when each call walked every link).
  The index follows these calls' own writes; when `Parent` changed any other way (a raw `Set` or `Get`, a removal, a destruction, the propagation's repair of an
  orphan) the world's `ComponentVersion<Parent>()` moved, and the next call makes the index again with one pass (`TransformReport::index_rebuilds`).
- **Helpers.** `LookAt(eye, target, up)` returns a `LocalTransform` whose -Z axis points at the target; `Decompose(matrix)` splits a matrix back into position, rotation and scale.

## Removal tracking

A component that declares `static constexpr bool kTrackRemoval = true` (and is trivially copyable and not a tag) has every removal logged by the world: destroying an entity that has it,
or removing the component, appends an entry with the entity, the value at that moment and a number `seq` that rises within the world. `world.RemovedCount<T>()` and `world.RemovedAt<T>(i)`
(`context.RemovedCount<T>()` in a system, which must declare `T` read) list the entries, oldest first. They live for the frame they were made in and the next one (`World::EndFrame`, which
`Schedule::Run` calls at the end of a frame), so a system that runs once per frame sees each exactly once if it remembers the largest `seq` it handled. Replacing a value with `Set` is not a removal.

## Rendering

Include `scene/render_sync.h` and link `scene` (module `engine/scene`, above `ecs` and `renderer`; neither includes it). It mirrors entities into a `renderer::RenderScene`, which is unchanged.

- **Components.** `scene::MeshRenderer { renderer::GpuMeshPtr mesh; u32 material; u32 flags; }` with the `WorldTransform` draws a mesh; the mesh is shared by every entity that draws it
  (prepare its upload barriers and `MarkReady` it, as for any mesh). `scene::Terrain` (a shared `renderer::TerrainSource` and a `TerrainMaterial`) draws a height map at the entity's `WorldTransform`, which must be a translation and a turn about y; `MakeTerrainSyncSystem` mirrors it into the render scene (adds it, replaces it when the source, the material, the placement or the `Hidden` tag changes, removes it when the component or the entity goes) and counts a missing source or a scaled or tilted placement as `invalid`. A world may hold several terrains. `scene::LightSource` makes a light (a point light at the entity's position, a directional one pointing along
  its `direction` turned by the entity's rotation). The `scene::Hidden` tag sets `ObjectFlags::kHidden`. An entity mirrors a mesh or a light, not both.
- **The system.** `MakeRenderSyncSystem(name)` in `ecs::kRender` needs the resources `SceneLink { RenderScene* }` and `SceneSyncReport`. It keeps a `RenderHandle` (the object and light ids, with removal
  tracking) on each mirrored entity and calls the scene only for what changed: `AddObject` for new entities, `SetTransform` when the matrix differs from the scene's, `SetFlags`, a remove and add
  when the mesh or material changed, `RemoveObject`/`RemoveLight` for destroyed entities or ones that lost the component. A scene with nothing changing costs the `Changed` queries and no calls.
- **Lifetime.** The sync holds a share of each object's mesh, so the geometry outlives its scene entry even if the entity was destroyed first.
- **Limits.** A full `RenderScene` is a counted refusal (`SceneSyncReport::refused_full`), retried each frame. A missing or empty mesh, or a light the scene would abort on (a point light without a radius,
  a value that is not finite) is skipped and counted in `invalid`. The linked scene must not change between frames.
- **The camera.** `MakeCameraSystem(name)` builds `CameraOutput` (a `renderer::Camera`) from the `ActiveCamera` resource's entity, which carries a `scene::Camera` and a `WorldTransform`
  (+X right, +Y up, -Z forward); `CameraOutput::valid` is false when there is none.

- **Spatial queries.** To ask what is near an entity, give it `SpatialBounds` and `SpatialMembership` and let the spatial sync keep the sets (see [Spatial partitioning](spatial.md), "Putting entities in the spatial world"); do not write a private grid in a system. `spatial::ForEachIn<Health>(world, spatial, set, region, filter, fn)` runs a function on the entities a spatial query returns, with their components ("Asking questions from gameplay code").

## Rules of thumb

- **Tag or data?** Use an empty struct (a tag) for a yes/no fact that queries filter on (`Frozen`, `Hidden`, `Active`). Adding or removing it moves the entity to another archetype, so it suits states that
  change rarely or in bulk, not a flag toggled for every entity every frame (churn costs more than in an object layout; see "Measured cost"). A per-frame flag belongs in a data component.
- **When to filter with `Changed<T>`.** When a system's work per row is larger than a load and a compare, and writes are clustered (a few chunks change, not a few rows in every chunk). Random writes touch
  most chunks and the filter saves little.
- **When to use a command buffer.** Whenever a system creates, destroys, adds or removes while it iterates: record into the buffer, apply after the loop. A direct structural change inside `ForEach` is refused.
- **Split components by how they change.** Data written every frame and data written rarely should be separate components, so stamping and `Changed<T>` stay precise and the hot loop reads less memory.
- **Keep queries.** Build a query once and reuse it; a kept query refreshes almost for free.
- **Parallelize heavy systems.** A system whose per-row work is around 100 ns or more scales to about 85 percent efficiency at 4 workers; a trivial loop is memory-bound and gains little.

## Measured cost

`game/ecs_bench` runs the workload of the design spec (section 6) on the ECS and on a naive object-oriented baseline (heap-allocated objects, virtual `Update`, optional components behind owning
pointers, visited in shuffled order) with the same arithmetic, and compares a checksum so neither side can win by doing less. Numbers below: AMD Ryzen 9 7950X3D (16 cores, 32 threads), Linux 7.2,
clang 23.1.4, Release (`-O3 -DNDEBUG`), unity build, commit `daa73b1` plus the benchmark files; median of 100 frames after 20 warm-up frames.
Run it yourself with `ecs_bench [--entities N] [--frames N] [--threads N] [--csv PATH]`.

| Scenario (100k entities unless noted) | ECS | Baseline | Ratio |
|---|---|---|---|
| Four-step iteration, 1k entities | 0.0029 ms | 0.0060 ms | 2.1x |
| Four-step iteration, 10k | 0.029 ms | 0.086 ms | 3.0x |
| Four-step iteration, 100k | 0.30 ms | 1.67 ms | 5.6x |
| Four-step iteration, 1M | 8.5 ms | 55 ms | 6.5x |
| Sparse access (1 percent carry a tag) | 0.0005 ms | 0.10 ms | 200x |
| `Changed<T>`, 1 percent written, clustered | 0.007 ms | 0.093 ms | 13x |
| `Changed<T>`, 1 percent written, random | 0.091 ms | 0.112 ms | 1.2x |
| Structural churn (10 percent created and destroyed, 10 percent tag toggled, per frame) | 1.58 ms | 0.65 ms | **0.41x (slower)** |
| Hierarchy propagation (chains of five, every root moved, sequential schedule) | 3.2 ms | 2.2 ms | **0.67x (slower)** |

- **Parallel scaling** of a heavy per-entity system (7.5 ms single-threaded in the baseline): 1 worker 2.0 ms; 2 workers 97 percent efficiency; 4 workers 85 percent; 8 workers 84 percent; 16 workers 79 percent;
  32 workers (all hardware threads, so shared cores) 48 percent.
- **Memory:** 110 bytes per entity for the workload's four components (Vec3, Vec3, spring, 4x4 matrix plus the 8-byte entity) against about 208 bytes for the baseline's two heap blocks. Creating an entity costs 32 ns
  and destroying one 17 ns (baseline 35 ns and 27 ns).
- **Change stamping** costs nothing measurable: the same 100k-position pass through a `Write` term and a `Read` term takes 0.0588 ms both ways.
- **`Changed<T>` is chunk precision.** Writes clustered in the first rows skip nearly everything; 1 percent of the entities written at random touches almost every chunk, so the filter then saves little. Keep
  data that changes together next to each other, or split the rarely written part into its own component.
- **Churn and hierarchy are slower than the baseline's equivalent.** A structural change moves a row between chunks (and a destroy swaps the last row in); the baseline replaces a pointer. Propagation recomputes
  every world matrix of a changed tree, parent before child, in depth order across chunks; the baseline walks its tree once. Both are listed in `KNOWN-ISSUES.md`.
- A sparse set was **not** added: the sparse case is 200x faster than the baseline, far from the 2x slower that the spec set as the threshold.
- **On Windows** (AMD Ryzen AI 7 350, 8 cores, MSVC Release): iteration at 100k 0.40 ms against 9.1 ms (22.7x), sparse access 119x, 4 workers at
  96 percent efficiency, 110 bytes per entity (baseline 232). Churn (1.45x) and hierarchy propagation (3.8x) beat the baseline there, because its heap allocations
  cost more with that system's allocator (155 ns a create against 35 ns on Linux).
- **On macOS** (Intel Core i5-8257U, 4 cores, 8 threads, a 2019 MacBook Pro; Apple clang 17, Release; one run while Spotlight was indexing): iteration
  at 100k 0.85 ms against 16.9 ms (19.8x), at 1M 9.1 ms against 338 ms (37x), sparse access 518x; 4 workers at 82 percent efficiency, 8 at 61 percent (8 threads
  on 4 cores); 110 bytes per entity (baseline 232). Churn beats the baseline (1.5x), as on Windows; hierarchy propagation does not (0.89x). Every pass condition holds.
  Again on an idle machine: iteration at 100k 0.88 ms against
  19.0 ms (21.5x), 4 workers at 80 percent, sparse access 529x; the pass conditions hold.

## Common mistakes

- **Keeping a pointer from `Get<T>` across an `Add`, `Remove`, `Create` or `Destroy`.** The row may have moved; fetch the pointer again.
- **Ignoring the returned `ecs::Status`.** `DeadEntity`, `AlreadyHas` and `Refused` are normal answers, not crashes.
- **Changing components while iterating.** A structural call during iteration fails an `ENGINE_ASSERT`; record it in a `CommandBuffer` and apply it after the loop.
- **Using a placeholder with another buffer or after `Clear`.** It resolves to nothing; resolve it right after `Apply`.
- **Expecting `Changed<T>` to name the changed entities.** It names chunks; every entity of a visited chunk is passed, changed or not.
- **Declaring less than the system touches.** Parallel systems then race; the violation counter and ThreadSanitizer are the net.
- **Calling `Build()` and forgetting to call it again after `Add`.** `Run` aborts when the order is stale.
- **Forgetting `Finish()`.** Without it `Since()` stays put and every pass sees all the changes since the start.
- **Writing a component through `Get` inside a query that writes it.** Use the argument the query gives you.
- **Building a query every frame.** A query keeps its match; make it once and keep it.
- **Storing a `ComponentId` on disk.** Ids depend on the order of first use; store the type's name.
- **Using an `Entity` as if it owned the entity.** It is only an id; destroying it is the world's job.

## See also

[Containers](foundation-containers.md), [Jobs](jobs.md) (the systems will run on them), `docs/superpowers/specs/2026-11-10-ecs-design.md`.
