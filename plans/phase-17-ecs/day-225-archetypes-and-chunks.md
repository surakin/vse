# Phase 17 / Day 225 — Archetypes & Chunk Storage

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Days 223-224.

The storage core: chunks of structure-of-arrays columns grouped by archetype.

## Tasks

- [ ] **`ecs::Chunk`:** a 16 KiB (value from Day 222) aligned block with a header (archetype, count, capacity, the entity column, per-column write ticks) and the component columns, each aligned to its type, laid out largest alignment first; capacity = the most rows whose columns fit; the layout is computed once per archetype (`ArchetypeLayout { column offsets, capacity }`); a degenerate archetype whose row exceeds a chunk is refused with a counted error.
- [ ] **`ecs::Archetype`:** `ComponentSet`, layout, a list of chunks (with a `partially_filled` cursor), `AddRow(Entity) -> RowRef`, `RemoveRow(chunk, row) -> moved Entity or none` (swap with the last row of the archetype's last chunk: relocate each column by `move_construct` + `destroy` or `MemCopy` for trivial columns, and the freed last chunk returns to the pool), `ChunkCount`, `EntityCount`, bytes used.
- [ ] **Chunk pool:** chunks come from a `ChunkPool` (fixed-size blocks from the engine's pool allocator, freed chunks kept up to a documented budget, then returned); no per-entity allocation ever; the pool is world-owned; memory counters (`chunks`, `bytes`, `pooled`).
- [ ] **Column access:** `Archetype::Column<T>(chunk) -> Span<T>` and the type-erased `ColumnPtr(chunk, id)`; the entity column is always column 0.
- [ ] **Tests** (`tests/ecs/archetype_test.cpp`): layout alignment for mixed types (a 1-byte, 4-byte, 16-byte-aligned and 64-byte-aligned component in one archetype: every column pointer aligned, columns do not overlap, capacity maximal); add 100k rows and read every value back; remove from the middle, the end and the only row; the moved entity is reported exactly when a row moved; non-trivial components are moved and destroyed exactly once (counting type); tags occupy no bytes; the pool reuses chunks and honors the budget; an oversized archetype is refused; a model-based randomized test (fixed seed, 200k operations of add/remove against a reference `HashMap<Entity, values>`) with the world invariants checked after every 1000 operations.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime or memory code (always for storage days); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md`: "Storage" (chunks, memory layout, cost).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** archetypes store rows in aligned SoA chunks with swap-remove, no per-entity allocation, exact object lifetimes, and a randomized model test passes under asan.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
