# Phase 10 / Day 132 — The `ConfigStore` Interface

**Phase:** [Phase 10 — Settings System](../phase-10-settings-system.md)
**Depends on:** Day 131.

The abstract class that makes the settings system independent of where values live (a file, the Windows registry, `NSUserDefaults`, a server), with the doubles and the conformance suite that prove it. New files `settings/config_store.h`, `memory_store.h/.cpp`; tests in `tests/settings/`.

## Tasks

- [ ] `settings::ConfigStore` (abstract, `ConfigStorePtr` = `UniquePtr<ConfigStore>`) as the spec shaped it: reading (`Get`, `Enumerate` in a stable order), writing (`Set`, `Remove`, `Commit` with the documented atomicity), `Reload`, `Capabilities()` (writable, preserves comments and order, atomic commit, per-key writes, can notify of external changes, case-sensitive keys, supports lists), `Describe()` for logs (what the store is, such as a path), errors as `settings::Status` values.
- [ ] `settings::StoredValue`: the typed value as a store holds it (text plus a type tag where the medium has types, text only where it does not); how a store that cannot type a value (a file) hands the registry a text and a store that can (a registry) hands a typed one; the one conversion function the registry uses.
- [ ] `MemoryConfigStore` (full-featured, in memory), `ReadOnlyConfigStore` (wraps entries from an array: the shipped defaults), and the test doubles `FailingConfigStore` (fails the Nth call with a chosen error), `SlowConfigStore` (delays and completes on its own thread to prove nothing assumes immediacy), `NoOrderConfigStore` (returns entries in a different order each call; models a registry).
- [ ] The **conformance suite** `ConfigStoreConformance(factory)` (header in `tests/settings/`, reused by Day 133's file store and by the platform stores later): set/get of every type, overwrite, remove, remove of a missing key, enumerate after changes, a key with every legal character, 1000 keys, commit then reload sees the values, a failed commit leaves the previous content, `Commit` of nothing, use after a failure, read-only stores refuse writes with the documented error; capabilities are honest (a store that says it preserves order is checked).
- [ ] Notification hook: `ConfigStore::SetChangeListener` (best effort, only when the capability says so), tested with the memory store firing from another thread.
- [ ] Tests (phase 10): the suite on every store above; the doubles' own behavior; TSan on the listener.
- [ ] **Documentation:** labels; the guide gets "Where values live" and "Writing a store backend" (built on the doubles); reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; record it.
- [ ] Update `STATUS.md`; commit.

**Done:** the interface, three real stores and three doubles pass one conformance suite, nothing in the interface is file-shaped, and the guide explains how to write a backend.
