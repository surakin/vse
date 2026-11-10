# Phase 17 / Day 224 — Component Types & the Registry

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Day 223.

What the world knows about a component type, with no RTTI.

## Tasks

- [ ] **`ecs::ComponentId`** (u16) and **`ecs::TypeInfo`** (`size`, `align`, `is_tag`, `trivially_relocatable`, `move_construct(dst, src)`, `destroy(p)`, `default_construct(p)` where the type has one, `name`: the type's `static constexpr const char* kComponentName` when it declares one, else `"component#N"` (no compiler-specific type-name tricks); `ComponentOf<T>()` returns the id, registering on first use with a process-wide thread-safe counter (one `Atomic<u16>`; the id is stable for the process, not across processes).
- [ ] **`ecs::ComponentRegistry`** per world: the `TypeInfo` table indexed by id (copied from the global table on first use in that world so a world never reads a table that is growing), cap 128 types with a counted refusal; `ComponentSet` (a 128-bit bitset: `Add`, `Remove`, `Has`, `IsSubsetOf`, `Hash`, iteration in id order).
- [ ] **Requirements enforced at compile time:** a component is move-constructible and destructible; references, arrays and const types are refused with a `static_assert`; alignment up to 64 bytes (cache line) supported, larger refused; tags (empty types) are columns of zero bytes and do not occupy chunk memory.
- [ ] **Tests** (`tests/ecs/component_test.cpp`): ids are distinct and stable across calls and threads (8 threads registering 100 types in different orders get one consistent id per type); `TypeInfo` of a trivial type, a non-trivial type (counts moves and destructions) and a tag; `ComponentSet` operations against a reference `Vector<bool>` (randomized, fixed seed); the cap is refused with a count; the compile-time refusals are checked with a negative-compile test (`tests/ecs/compile_fail/`, run by CMake with `try_compile`).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime or memory code (always for storage days); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md`: "Components".
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** component types register without RTTI, carry exactly the operations the storage needs, and the limits and misuse cases are enforced and tested.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
