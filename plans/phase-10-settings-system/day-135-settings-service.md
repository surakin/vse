# Phase 10 / Day 135 — The Settings Service: Loading, Change Notification, Saving, Threads

**Phase:** [Phase 10 — Settings System](../phase-10-settings-system.md)
**Depends on:** Day 134; Phase 2 (jobs, for background saving).

The object an application owns: it builds the layer stack, publishes values to the typed handles, tells interested code what changed, and saves. New files `settings/settings.h/.cpp`; tests in `tests/settings/`.

## Tasks

- [ ] `settings::Settings` (owned by the application, not a global): `Load(const LoadOptions&)` builds the standard stack (defaults, system, user at `io::UserConfigDirectory(app)/settings.cfg`, project, environment, command line), reads every layer, converts and validates against the registry, and publishes one immutable snapshot; the **report** lists every problem (layer, key, line, reason) and whether each layer loaded, was damaged, or was missing. A problem in one layer never stops the others.
- [ ] Publishing and reading as Spike B decided: handles read the current snapshot at the chosen cost; `Settings::Snapshot()` gives a consistent view of all values for code that must read several; a reader on a worker thread never sees a half-applied change (test with a writer that flips two related settings and a reader that checks the invariant, TSan).
- [ ] **Changing a setting:** `Settings::Set(key, value)` validates (range policy, enum, capability), writes the write layer in memory, publishes a new snapshot, and calls the **change listeners** (`AddListener(key-or-prefix, callback)` on the thread that called `Set`, after publishing, in registration order; a listener may call `Set` (re-entrancy rule from the spec) and may veto nothing); a `SettingsChange` list groups several changes made by one `Apply` call so a consumer sees them together.
- [ ] **The apply step:** each setting's apply class and `restart_required` are passed to the listener with the change; `Settings::Apply(changes, ApplyFn)` is the hook the renderer's `ApplySettings` plugs into (Day 137); a change a consumer reports as failed is rolled back in the snapshot and the write layer, and the report says so.
- [ ] **Saving:** `Settings::Save()` (explicit), `SaveIfChanged()`, and the policy options (`SaveOnExit` through a scope guard, `Autosave` after N seconds of quiet using `jobs` and `io` asynchronous writes — off by default); saving writes only the write layer's changes (via `Commit`), never the layers above it (a `--set` is not saved unless asked: `Save(IncludeCommandLine)`), keeps `.bak`, and reports a read-only home directory as a status the application can show, not a crash.
- [ ] Crash safety: the settings text of the current process (key, value, provenance, no secrets) is published to the crash report through the crash-state facility (`debug::SetCrashStateText("settings", ...)`), capped in size.
- [ ] Tests (phase 10): load with every combination of missing, damaged and healthy layers; the report text; `Set` validation and rollback; listener order, re-entrancy, prefix subscriptions; two threads setting and reading (TSan, 10 000 changes); `Save` after load changes only the changed lines of the file; `Save` with `--set` values present; read-only directory; a `Settings` over the failing-store double; autosave with the scheduler running; the crash-state text.
- [ ] **Documentation:** the guide gets "Loading, changing and saving" and "Reacting to a change" with run samples; labels; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`. Extra scrutiny: the listener re-entrancy, the snapshot publication, rollback.
- [ ] Update `STATUS.md`; commit.

**Done:** an application loads, reads (from any thread), changes, observes and saves its settings through one object, with every problem reported and the user's file never lost.
