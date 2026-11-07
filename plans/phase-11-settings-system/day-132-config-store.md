# Phase 11 / Day 132 — The `ConfigStore` Interface

**Phase:** [Phase 11 — Settings System](../phase-11-settings-system.md)
**Depends on:** Day 131.

The abstract class that makes the settings system independent of where values live (a file, the Windows registry, `NSUserDefaults`, a server), with the doubles and the conformance suite that prove it. New files `settings/config_store.h`, `memory_store.h/.cpp`; tests in `tests/settings/`.

## Tasks

- [x] `settings::ConfigStore` (abstract, `ConfigStorePtr` = `UniquePtr<ConfigStore>`) as the spec shaped it: reading (`Get`, `Enumerate(prefix)`; the order is unspecified unless the store preserves order, because `NoOrderConfigStore` models a registry that has none), writing (`Set`, `Remove`, `Commit` with the documented atomicity), `Load` (called again it reloads; the spec merged `Reload` into it), `Capabilities()` (writable, preserves order, atomic commit, per-key writes, can notify of external changes, typed values; no case-sensitivity bit because all keys are lowercase, and no lists, a Day 130 decision), `Describe()` for logs (what the store is, such as a path), errors as `settings::Status` values.
- [x] `settings::StoredValue`: the typed value as a store holds it (text plus a type tag where the medium has types, text only where it does not); how a store that cannot type a value (a file) hands the registry a text and a store that can (a registry) hands a typed one; the one conversion function the registry uses.
- [x] `MemoryConfigStore` (full-featured, in memory), `ReadOnlyConfigStore` (wraps entries from an array: the shipped defaults), and the test doubles `FailingConfigStore` (fails the Nth call with a chosen error), `SlowConfigStore` (delays and completes on its own thread to prove nothing assumes immediacy), `NoOrderConfigStore` (returns entries in a different order each call; models a registry).
- [x] The **conformance suite** `ConfigStoreConformance(factory)` (header in `tests/settings/`, reused by Day 133's file store and by the platform stores later): set/get of every type, overwrite, remove, remove of a missing key, enumerate after changes, a key with every legal character, 1000 keys, commit then reload sees the values, a failed commit leaves the previous content, `Commit` of nothing, use after a failure, read-only stores refuse writes with the documented error; capabilities are honest (a store that says it preserves order is checked).
- [x] Notification hook: `ConfigStore::SetChangeListener` (best effort, only when the capability says so), tested with the memory store firing from another thread.
- [x] Tests (phase 11): the suite on every store above; the doubles' own behavior; TSan on the listener.
- [x] **Documentation:** labels; the guide gets "Where values live" and "Writing a store backend" (built on the doubles); reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors; quote the lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`; record it.
- [x] Update `STATUS.md`; commit.

**Done:** the interface, three real stores and three doubles pass one conformance suite, nothing in the interface is file-shaped, and the guide explains how to write a backend.


## Result

- New: `settings/config_store.h/.cpp` (`ConfigStore`, `Status`/`StoreError`, `StoredValue`, `ToStored`/`FromStored`, capabilities, `ConfigStorePtr`), `settings/memory_store.h/.cpp` (`MemoryBacking`, `MemoryConfigStore`, `ReadOnlyConfigStore`), `ValidateValue` in `desc.h`; test doubles `FailingConfigStore`, `SlowConfigStore`, `NoOrderConfigStore` and `PartialCommitConfigStore` in `tests/settings/store_doubles.h`; the suite in `tests/settings/config_store_conformance.h`; guide sections "Where values live" and "Writing a store backend".
- Deviations from the plan: two real stores (memory, read-only) now and the file store on Day 133, not three; `Reload` is `Load`; no lists and no case-sensitivity bit; the doubles own their inner store.
- Not done: no store with an atomic commit is made to fail partway (the memory store has no failure path in `Commit`), so `kCapAtomicCommit` is checked only through `FailingConfigStore`'s contract test; the `Aborts:` on `ToStored` is not death-tested (KNOWN-ISSUES).
- Runs: plain `ran 1198 of 3122 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 test fails); asan 867 of 3122; tsan 584 of 3122; docs lint 3745/3745. The review fixes were re-run with the three settings executables (19475 assertions, 30 cases, all pass).

## Post-implementation code review

`/code-review high`: 9 findings. Fixed: `FromStored` did not check a typed value's range or enum index (new `ValidateValue`, tests); `Set`/`Remove` logs grew without bound (coalesced per key); `ReadOnlyConfigStore` crashed on a null text and listed repeated keys twice; Catch2 `REQUIRE` inside `std::thread` bodies (now plain atomics); the meaningless `kCapCaseSensitive` bit (removed); the plan deviations (this file and the spec); added `PartialCommitConfigStore` to test the `done`/`total` contract. Declined: listeners are called while holding the backing's notify mutex (kept, so no listener runs after a destructor returns; the header and guide now say a listener must only record the change and must not create or destroy stores); the reviewer's non-finite float case cannot occur (`Value::FromFloat` refuses NaN and infinity).
