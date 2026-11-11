# Phase 19 / Day 258 — Stable Identities & Entity References

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Days 256-257; Phase 17 (`Parent`, the entity table).

Name an entity across saves, processes and machines, and store references between entities safely.

## Tasks

- [ ] **`storage::EntityKey`:** 128 bits, value type with ordering, hash and text form; `KeyGenerator` draws from the engine's randomness wrapper (added to `os/` first if Day 254 had not), and a seeded deterministic generator exists for tests.
- [ ] **`EntityKeyComponent`** registered with a stable id; a `KeyIndex` resource maps key to `ecs::Entity` and back, kept in step by a system from the ECS change stamps and removal log (the pattern of the spatial sync); a duplicate key is rejected with a `Status` (never two entities with one key).
- [ ] **`EntityRef`:** the field type of Day 256 completed: stored as a key (the null key for a null reference), decoded into a pending-reference list and resolved after every entity of a load exists; `ecs::Parent` is registered as a reference field.
- [ ] **Missing targets:** a reference to a key that is not in the stored set nor in the target world yields `MissingReference` carrying the first missing keys (up to a cap); the load option `NullMissingReferences` turns it into a null reference plus a count in the report.
- [ ] **Remapping:** loading entities that were saved with keys already present in the world (Merge) keeps the world's entity ids and rewires references through the key index; loading into an empty world creates entities in a deterministic order (decided in the spec).
- [ ] **Tests** (`tests/storage/identity_test.cpp`): keys are unique over 1,000,000 draws; the index follows create, destroy and key change across 1,000 random frames and equals a brute-force scan; references round trip through key resolution on a hierarchy of 5 levels and 10,000 entities; cycles of references (a parent loop) are rejected by the ECS rules and reported; a missing target in both option settings; two entities with one key.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** every entity can have a durable identity, references between entities are stored by it, and a reference to nothing is an error value.
