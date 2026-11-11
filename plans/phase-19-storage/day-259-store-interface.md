# Phase 19 / Day 259 — The Storage Interface & the Reference Store

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Days 256-258; Phase 2 (jobs).

Define `storage::Store` and the options it takes, and write the reference implementation every other store is compared with.

## Tasks

- [ ] **`storage::Store`** (abstract; record level): `Put(EntityRecord)`, `Get(key)`, `Remove(key)`, `Scan(range, visitor)`, `Count`, `Begin`/`Commit`/`Rollback` for an all-or-nothing batch, `Metadata` get and set (format version, schema version, last save label), `List`; an `EntityRecord` is a key plus the encoded components `{component id, version, bytes, hash}`; all errors are `storage::Status` values (`NotFound`, `Exists`, `Busy`, `Corrupt`, `TooNew`, `LimitExceeded`, `MissingReference`, `Cancelled`, `Io`, `Other`).
- [ ] **Options and results:** `Selection` (all entities with a key, a set of keys, an ECS query plus a filter callback), `SaveOptions` (full or incremental, label), `LoadOptions` (create, replace or merge; unknown-component policy; missing-reference policy), `Report` (counts created, replaced, skipped, nulled, bytes), `Progress` callback and a `CancelToken` checked between batches.
- [ ] **`storage::WorldStorage`** declared (non-virtual; implemented on Days 260-261): `Save`, `Load`, `Delete`, `List`, the asynchronous `SaveAsync`/`LoadAsync` forms declared and implemented on Day 264.
- [ ] **`storage::MemoryStore`:** the reference record store (sorted by key, atomic batches by an undo log); correct and simple rather than fast.
- [ ] **Store conformance suite** (`tests/storage/store_conformance.h`, template over a store factory): put/get/remove/scan round trips; scan order is key order; a batch commits all or nothing (injected failure at every operation); metadata persists; a corrupt record is `Corrupt` not abort; concurrent readers during a batch see the old state; runs against `MemoryStore` now, `SqlStore` on Days 262-263.
- [ ] **Thread-safety contract** written in the headers: one writer at a time, readers allowed between batches; which calls are thread-safe.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the storage interface, its options and its error vocabulary are fixed, and a reference store passes the store conformance suite.
