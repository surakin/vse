# ECS (Phase 17) — design

**Status:** settled on Day 222 (2026-11-10). Later days amend this document in the same commit as any change.

## 1. Intent

A data-oriented entity/component/system core that gameplay, the scene and later systems (physics, audio, scripting, the editor, 3D text entities) build on, replacing Phase 5's hard-coded scene list as the thing a program fills. The renderer keeps its input type: `renderer::RenderScene` is unchanged and a bridge module mirrors entities into it.

Goals: thousands to millions of entities per frame; cache-friendly iteration; structural changes that are safe during iteration; systems that run in parallel through the job system with a deterministic result; honest measurement against a naive object-oriented baseline.

Non-goals (this phase): serialization and prefabs from files (Phase 22), events and observers (added when the first consumer needs them), entity relationships other than `Parent`, replication, sparse-set storage (decided by a benchmark on Day 235), editor reflection.

Anti-goals: an ECS that wins only on a toy benchmark; a scheduler whose parallel result differs from the sequential one; hidden threads (all parallelism is the job system's, `PLAN.md` assumption #15).

## 2. Decisions

| Topic | Decision | Reason / evidence |
|---|---|---|
| Storage | Archetypes (one per distinct component set) of fixed-size **32 KiB chunks**, structure-of-arrays columns inside a chunk, rows filled in creation order; remove = swap with the last row of the archetype | experiment (a): 1M entities, 5-column loop: 16 KiB 1.14 ns/entity, 32 KiB 0.94-1.00, 64 KiB 0.81-0.83; at 100k all within 3 percent (0.70-0.75). 32 KiB keeps 111 chunks at 100k entities (enough work items for 8 workers) and wastes at most 32 KiB per sparse archetype; 64 KiB is faster in a memory-bound scan but coarsens parallel grain. A compile-time constant, revisited by the Day 235 benchmark |
| Entity id | 64 bits: 32-bit index + 32-bit generation, all-zero invalid; table of (generation, archetype, chunk, row) | experiment (b): generation check costs 0.13 ns per random lookup (0.75 vs 0.62 ns) and 0.18 ns sequential: negligible against stale-id safety (as `renderer::ObjectId` and the RHI handles) |
| Component types | registered at first use with a process-wide counter (no RTTI, exceptions stay off); `TypeInfo {size, align, is_tag, trivially_relocatable, move_construct, destroy, default_construct, name}`; non-trivial components allowed (`SharedPtr` members); tags have zero size; 128 component types per world, 32 per archetype, alignment up to 64 | the storage needs only these operations |
| Relocation | trivially relocatable columns move by `MemCopy`/`MemMove`; others by `move_construct` + `destroy` through the function pointers | experiment (c): a function-pointer move of a 64-byte row costs 7.9 ns versus 6.8 ns for `memcpy` (+15 percent) in a swap-remove loop: worth the flag |
| Structural change | direct calls legal outside iteration; during iteration through a `CommandBuffer` applied at a sync point; a direct call during iteration is an `ENGINE_ASSERT` | the classic invalidation hazard turned into a deterministic failure |
| Queries | typed terms `Read<T>`, `Write<T>`, `Optional<T>`, `With<T>`, `Without<T>`, `Changed<T>`, `Added<T>`; matching archetype list cached per query and extended from an archetype version counter; per-entity `ForEach` and per-chunk `ForEachChunk` (spans; the vectorizable and parallelizable form) | |
| Change detection | world tick plus per-column, per-chunk written/added ticks; precision is the chunk (documented) | cheap, enough for the render sync; per-entity bits would cost a column |
| Systems | function + declared access (components, resources) + stage + ordering constraints + optional run condition; stages `kPreUpdate`, `kUpdate`, `kPostUpdate`, `kRender` | access sets are what makes parallelism safe and checkable |
| Parallelism | systems whose access sets do not conflict form **waves** that run as jobs joined by a `Counter`; a later system never overtakes an earlier conflicting one; one system's chunks run through `ParallelFor`; per-job command buffers merge in chunk order | the result of a frame equals the sequential run (hash of all component bytes) |
| Resources | one singleton per type per world, access-declared like components | |
| Transforms | `LocalTransform`, `WorldTransform`, `Parent`; propagation parent-first, parallel per depth, writes only on difference | exact change marking for the render sync |
| Renderer seam | `engine/scene` (above `ecs` and `renderer`): `MeshRenderer`, `Light`, `Camera`, `RenderHandle`, a `RenderSync` system using `Changed<T>` and removal tracking; `RenderScene` unchanged | the renderer's own tests keep using `RenderScene` without `engine/scene` |
| Settings | `ecs.parallel_systems` (default true), `ecs.stats` (default false) | CLAUDE.md: anything configurable is a setting |
| Wrappers | all code uses `lang`/`sync`/`cstr`/`os` and the engine containers, never `std` | CLAUDE.md, Phase 16 |

## 3. Layout

```
EntityTable: slot -> { u32 generation; u32 archetype; u32 chunk; u32 row }
Archetype  : ComponentSet (128-bit), layout, Vector<Chunk*>, transition graph (component, add|remove) -> Archetype*
Chunk (32 KiB, 64-aligned): header {archetype, count, capacity, per-column written/added tick}
             | entities[cap] | column A[cap] | column B[cap] | ... (largest alignment first)
World      : EntityTable, archetypes (HashMap<ComponentSet, Archetype>), ChunkPool, ComponentRegistry,
             resources, tick, iteration depth, removal lists (opt-in types)
```

Capacity = the most rows whose columns fit after the header; an archetype whose single row exceeds a chunk is refused (counted). Chunks come from a world-owned `ChunkPool` (the engine's pool allocator; freed chunks are kept up to a budget).

## 4. Public API sketch

```cpp
ecs::World world;
Entity e = world.Create<LocalTransform, Velocity>(LocalTransform{...}, Velocity{...});
world.Add<Frozen>(e, {});
auto q = world.Query<Write<LocalTransform>, Read<Velocity>, Without<Frozen>>();
q.ForEach([&](Entity, LocalTransform& t, const Velocity& v) { t.position += v.value * dt; });
q.ForEachChunk([&](ChunkView& c) { /* spans */ });
CommandBuffer cb; cb.Destroy(e); world.Apply(cb);
Schedule schedule; schedule.Add(MakeSystem("move", q, fn).InStage(kUpdate)); schedule.Build(); schedule.Run(world, dt);
```

## 5. Contracts

- **Thread safety.** `World` is thread-compatible. During `Schedule::Run`, systems of one wave read and write disjoint columns by construction (checked in debug). `Entity` and `Query` objects are plain data (a query object has one user at a time). `EntityTable` reads are concurrent-safe; allocation and free are exclusive.
- **Pointer validity.** `Get<T>` pointers and chunk spans are valid until the next structural change.
- **Determinism.** A frame's world state equals the sequential run for systems whose result does not depend on iteration order; order-sensitive reductions use `ReduceChunks`. Entity creation order and chunk order are stable.
- **Failure.** Dead entities and missing components are statuses, never crashes; exhausted limits (entities, types, chunk size, depth) are counted refusals; misuse during iteration and undeclared access are `Aborts:` assertions in debug builds and counted warnings in release.

## 6. Benchmark definition (fixed now, measured on Day 235)

Baseline: `Vector<UniquePtr<GameObject>>`, virtual `Update(dt)`, components as members and `Vector<UniquePtr<Component>>` for optional ones, each object heap-allocated in creation order and then shuffled. Work in both implementations: integrate position by velocity; a damped spring on a second component; a transform-to-matrix update; a conditional on a tag. Sizes 1k, 10k, 100k, 1M; median and p95 of 200 frames after warm-up; checksums of the results compared. Pass condition: ECS iteration at least **3x** faster than the baseline at 100,000 entities, and parallel efficiency at least **70 percent on 4 workers** for the heavy system. A miss is profiled, fixed, re-run and, if still missed, filed and presented to the user.

## 7. Experiments (2026-11-10, Linux, clang -O2, throwaway programs)

(a) chunk size, 5 columns (pos, vel, hp, tag, entity): rows per chunk and ns per entity for the movement loop (hp-only loop in parentheses).

| Chunk | 100k | 1M |
|---|---|---|
| 4 KiB | 0.83 (0.077) | 2.34 (0.126) |
| 16 KiB | 0.72-0.75 (0.07) | 1.13-1.14 (0.12) |
| 32 KiB | 0.71-0.72 (0.054) | 0.94-1.00 (0.090) |
| 64 KiB | 0.70-0.71 (0.052) | 0.81-0.83 (0.08) |
| 256 KiB | 0.68 (0.049) | 0.73 (0.069) |

(b) entity lookup, 1M random ids: generation-checked 0.75 ns, index only 0.62 ns; sequential 0.40 and 0.22 ms per million.
(c) swap-remove style relocation of 64-byte rows, 512k moves: `MemCopy` 6.8 ns, function pointer 7.9 ns.

Decisions follow the table in section 2. The numbers are single-machine and relative; Day 235 re-measures on the final code.

## 8. Module placement

`engine/ecs` (foundation + jobs; layer 10 beside `jobs` and `text`, edge `ecs -> jobs` allowed) and `engine/scene` (`ecs` + `renderer`; layer 13, beside the renderer's consumers). `engine/ecs` includes no renderer, RHI or platform header. Each has plain/asan/tsan library flavors. Tests: `tests/ecs`, `tests/scene` (phase 17). Benchmark program: `game/ecs_bench`.

## 9. Risks

- Parallel scheduling hides data races behind a wrong access declaration: mitigated by the debug access checker and tsan stress.
- Per-chunk change precision may over-mark: measured on Day 229 and in the render sync.
- Archetype explosion (many component combinations): bounded by 128 types; the archetype count is a stat and the graph cache keeps transitions O(1).
- Pointer invalidation bugs in user code: the iteration guard and `Validate()`.
- 3D text (Phase 28) needs `Text3D` entities: Day 402 of that phase is already amended to build on `engine/scene`.
