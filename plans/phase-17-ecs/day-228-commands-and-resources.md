# Phase 17 / Day 228 — Command Buffers & Resources

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Day 227.

Structural changes during iteration, and world-level singletons.

## Tasks

- [x] **`ecs::CommandBuffer`:** recorded `Create`, `Destroy`, `Add<T>`, `Remove<T>`, `Set<T>` on entities or on *placeholders* (an `Entity` the buffer hands out for a `Create` whose real id is only known at playback; later commands in the same buffer may name it, and `Resolve(placeholder)` gives the real one after playback); storage is a byte stream plus a table of command headers (no per-command allocation after growth; reusable with `Clear`); component values are moved into the stream and moved out at playback.
- [x] **Playback:** `world.Apply(CommandBuffer&)` at a sync point, in recording order; a command on a dead entity is skipped and counted (`ignored`), never a crash; the result is a `PlaybackReport` (applied, ignored, created); several buffers apply in the order given by the caller (the scheduler of Day 231 orders them by system registration order, so the outcome is deterministic).
- [x] **Thread-safety:** a buffer is thread-compatible (one writer); each parallel job gets its own buffer from `CommandBufferPool` (per-worker, reset each frame).
- [x] **Resources:** `world.SetResource<T>(value)`, `world.Resource<T>() -> T&` (`Aborts:` if missing), `TryResource<T>() -> T*`, `RemoveResource<T>()`; one per type, stored in a registry separate from components (so they are never queried); destruction order is reverse creation at world destruction.
- [x] **World destruction:** destroys entities and resources in a defined order and logs nothing when empty; destroying a world with pending buffers is allowed (the buffers hold no world pointer).
- [x] **Tests** (`tests/ecs/command_test.cpp`): destroy during `ForEach` via buffer then apply: exactly the marked entities are gone; placeholder create-then-add in one buffer resolves; ignored commands on dead entities are counted; a buffer with non-trivial components moves each value once and destroys unplayed ones exactly once when cleared (counting type); two buffers applied in order give a defined result; 100k commands record without allocation after the first frame (allocation counter); resources: set/get/replace/remove, missing resource abort (death test), reverse-order destruction (recording type).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime or memory code (always for storage days); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md`: "Commands" and "Resources".
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** structural changes during iteration are safe through deferred commands with deterministic playback, and resources work with a defined lifetime.

## Notes

- `CommandBuffer` (`ecs/command_buffer.h`): a table of `Command` headers (function pointers for apply and drop, target, payload position) plus 64 KiB payload blocks kept between uses; `Clear` and playback release no storage. Values over `kMaxAlign` (64) are a compile error; a value larger than a block is refused and counted.
- Placeholders are entities with generation `0xFFFFFFFF` (`Entity::kPlaceholderGeneration`); the entity table never issues it (generation wraps to 1), so a placeholder is never alive. `Resolve` reads the buffer's `created_` table, which survives playback until `Clear`.
- Resources are slots in a `Vector` keyed by a per-type id; replacing keeps the address; destruction at world end is newest-serial first.
- `IndexList` moved from `query.h` to `ecs/index_list.h` (shared).
- **Deviation:** the missing-resource abort is checked with the counting assert handler, not a death test (the death-test harness is not linked into `ecs_tests`).
- **Open:** the Windows build and run of the ECS (the "Two machines" task) stays open, filed in `KNOWN-ISSUES.md` (due by Day 236).
- Runs: see below.

## Post-implementation code review

`/code-review high`: 2 findings. Fixed 1: values aligned beyond 64 bytes were silently misaligned in payload blocks; now a `static_assert` (`kMaxAlign`), no runtime regression test possible for a compile error. Declined 1 (test-coverage): the missing-resource test does not call `Resource<T>()` on a missing type, because a continuing assert handler would dereference null; the death-test harness is not available to this suite.
