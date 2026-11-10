# Phase 17 / Day 228 — Command Buffers & Resources

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Day 227.

Structural changes during iteration, and world-level singletons.

## Tasks

- [ ] **`ecs::CommandBuffer`:** recorded `Create`, `Destroy`, `Add<T>`, `Remove<T>`, `Set<T>` on entities or on *placeholders* (an `Entity` the buffer hands out for a `Create` whose real id is only known at playback; later commands in the same buffer may name it, and `Resolve(placeholder)` gives the real one after playback); storage is a byte stream plus a table of command headers (no per-command allocation after growth; reusable with `Clear`); component values are moved into the stream and moved out at playback.
- [ ] **Playback:** `world.Apply(CommandBuffer&)` at a sync point, in recording order; a command on a dead entity is skipped and counted (`ignored`), never a crash; the result is a `PlaybackReport` (applied, ignored, created); several buffers apply in the order given by the caller (the scheduler of Day 231 orders them by system registration order, so the outcome is deterministic).
- [ ] **Thread-safety:** a buffer is thread-compatible (one writer); each parallel job gets its own buffer from `CommandBufferPool` (per-worker, reset each frame).
- [ ] **Resources:** `world.SetResource<T>(value)`, `world.Resource<T>() -> T&` (`Aborts:` if missing), `TryResource<T>() -> T*`, `RemoveResource<T>()`; one per type, stored in a registry separate from components (so they are never queried); destruction order is reverse creation at world destruction.
- [ ] **World destruction:** destroys entities and resources in a defined order and logs nothing when empty; destroying a world with pending buffers is allowed (the buffers hold no world pointer).
- [ ] **Tests** (`tests/ecs/command_test.cpp`): destroy during `ForEach` via buffer then apply: exactly the marked entities are gone; placeholder create-then-add in one buffer resolves; ignored commands on dead entities are counted; a buffer with non-trivial components moves each value once and destroys unplayed ones exactly once when cleared (counting type); two buffers applied in order give a defined result; 100k commands record without allocation after the first frame (allocation counter); resources: set/get/replace/remove, missing resource abort (death test), reverse-order destruction (recording type).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime or memory code (always for storage days); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md`: "Commands" and "Resources".
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** structural changes during iteration are safe through deferred commands with deterministic playback, and resources work with a defined lifetime.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
