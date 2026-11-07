# Phase 11 / Day 135 — The Settings Service: Loading, Change Notification, Saving, Threads

**Phase:** [Phase 11 — Settings System](../phase-11-settings-system.md)
**Depends on:** Day 134; Phase 2 (jobs, for background saving).

The object an application owns: it builds the layer stack, publishes values to the typed handles, tells interested code what changed, and saves. New files `settings/settings.h/.cpp`; tests in `tests/settings/`.

## Tasks

- [x] `settings::Settings` (owned by the application, not a global): `Load(const LoadOptions&)` builds the standard stack (defaults, system, user at `io::UserConfigDirectory(app)/settings.cfg`, project, environment, command line), reads every layer, converts and validates against the registry, and publishes one immutable snapshot; the **report** lists every problem (layer, key, line, reason) and whether each layer loaded, was damaged, or was missing. A problem in one layer never stops the others.
- [x] Publishing and reading as Spike B decided: handles read the current snapshot at the chosen cost; `Settings::Snapshot()` gives a consistent view of all values for code that must read several; a reader on a worker thread never sees a half-applied change (test with a writer that flips two related settings and a reader that checks the invariant, TSan).
- [x] **Changing a setting:** `Settings::Set(key, value)` validates (range policy, enum, capability), writes the write layer in memory, publishes a new snapshot, and calls the **change listeners** (`AddListener(key-or-prefix, callback)` on the thread that called `Set`, after publishing, in registration order; a listener may call `Set` (re-entrancy rule from the spec) and may veto nothing); a `SettingsChange` list groups several changes made by one `Apply` call so a consumer sees them together.
- [x] **The apply step:** each setting's apply class and `restart_required` are passed to the listener with the change; `Settings::Apply(changes, ApplyFn)` is the hook the renderer's `ApplySettings` plugs into (Day 137); a change a consumer reports as failed is rolled back in the snapshot and the write layer, and the report says so.
- [x] **Saving:** `Settings::Save()` (explicit), `SaveIfChanged()`, and the policy options (`SaveOnExit` through a scope guard, `Autosave` after N seconds of quiet using `jobs` and `io` asynchronous writes — off by default); saving writes only the write layer's changes (via `Commit`), never the layers above it (a `--set` is not saved unless asked: `Save(IncludeCommandLine)`), keeps `.bak`, and reports a read-only home directory as a status the application can show, not a crash.
- [x] Crash safety: the settings text of the current process (key, value, provenance, no secrets) is published to the crash report through the crash-state facility (`debug::SetCrashStateText("settings", ...)`), capped in size.
- [x] Tests (phase 11): load with every combination of missing, damaged and healthy layers; the report text; `Set` validation and rollback; listener order, re-entrancy, prefix subscriptions; two threads setting and reading (TSan, 10 000 changes); `Save` after load changes only the changed lines of the file; `Save` with `--set` values present; read-only directory; a `Settings` over the failing-store double; autosave with the scheduler running; the crash-state text.
- [x] **Documentation:** the guide gets "Loading, changing and saving" and "Reacting to a change" with run samples; labels; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors; quote the lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`. Extra scrutiny: the listener re-entrancy, the snapshot publication, rollback.
- [x] Update `STATUS.md`; commit.

**Done:** an application loads, reads (from any thread), changes, observes and saves its settings through one object, with every problem reported and the user's file never lost.


## Result

- New: `settings/settings.h/.cpp` (`Settings`, `LoadOptions`/`LoadReport`, `Change`/`ChangeEvent`, `SetResult`, `SettingsSnapshot`, `SaveOptions`, `SaveOnExit`), `kSecret` flag; tests `settings_test.cpp` (load with every combination of missing/damaged/healthy user and project files, provenance, validation and capability, listeners (order, prefix, batches, apply class, restart flag, re-entrancy to depth 8), rejected apply with rollback, snapshot consistency under 3 readers and 10000 batches, 4 writer threads x 2500 `Set` under TSan, `Save` changing only the changed lines, `--set` not saved, read-only directory, damaged file, failing layer, autosave and `SaveOnExit`, the crash-report text); guide sections "Loading, changing and saving" and "Reacting to a change".
- Deviations: autosave is `SetAutosaveQuietSeconds` plus `PollAutosave()` called by the application (no scheduler or asynchronous write: it needs neither jobs nor a thread, and the save is a short atomic write); `Load` can be replaced by `LoadFromLayers` for a hand-built stack; a runtime `Set` against a value a higher layer overrides is published (the session's explicit change wins) and `Provenance` reports it as `runtime`.
- Runs: plain `ran 1254 of 3290 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 test fails); asan `ran 923 of 3290`; tsan `ran 640 of 3290`; settings executables 30429 assertions in 86 cases; docs lint 3952/3952.

## Post-implementation code review

`/code-review high` (extra scrutiny on re-entrancy, snapshot publication and rollback): 10 findings. The design changed for the first four: one recursive lock now covers an operation including its apply function and listeners, and values are published only after the apply function accepts, so a rejected batch is undone before anyone can see or change anything, events arrive in publish order, and a rollback cannot overwrite another thread's change; the capability callback runs with no data lock held. Also fixed (each with a test): autosave retried a failing save on every poll (now backs off one quiet period); a missing write layer made every `Set` fail (now in-memory only); `Save` with the command line left those values in the write layer after a failed save (undone); a rollback restores exactly what the write layer held (an out-of-range line is put back byte for byte) and no longer marks the settings dirty; the command line was parsed twice (once); the snapshot rebuild is one function; crash-report entries are cleared only when used and a `kSecret` setting shows no value. Declined: none.
