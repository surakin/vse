# ECS: entities, components and systems

Status: draft (Phase 17, Day 223: entities; the rest of the page arrives with the days that build it).

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
