# Log devices / Day 4: settings, the C API observer as a device, shutdown

**Series:** [log devices](../log-devices.md)
**Depends on:** Days 1 to 3.

## Tasks

- [x] **Tests first** (`tests/settings/log_device_settings_test.cpp`, tag `[log][settings]`; `tests/engine_library` for the C API case):
  - `log.stdout=false` removes the stdout device, `true` restores it;
  - `log.color=never` makes the console device the plain stdout device, `always` the TTY device, `auto` decides from the terminal (Day 2);
  - `log.file.path` set creates a file device and lines reach the file; cleared, the device is removed and flushed; an unopenable path leaves
    the other devices working and records one `Warn` line;
  - `log.file.level`, `log.file.max_bytes` and `log.file.keep` apply on the next change event (rotation limits take effect without a restart);
  - the C API log callback still receives every line (the observer as before, or as a device) and `engine_destroy` removes its device and
    leaves the registry as it found it (two create/destroy cycles);
  - the C API ABI snapshot is unchanged.
- [x] **Settings** in `engine/foundation/settings/engine_settings.cpp`: register `log.stdout` (bool, default on), `log.color` (`auto`, `always`, `never`; default `auto`), `log.file.path` (string, empty
  = off), `log.file.level` (the verbosity enum), `log.file.max_bytes`, `log.file.keep`, group "log", `kAdvanced`; `AttachLogSettings` owns
  the file device (a `UniquePtr` in the attachment) and updates it from the listener.
- [x] **The `engine_c` observer**: keep `SetLogObserver` working; decide in this task, and record it here, whether the C API's callback becomes a
  `LogDevice` (preferred, if the 'handled suppresses stdout' rule can be kept) or stays the observer.
- [x] **Shutdown**: every device a library registers is removed in its teardown (Phase 23 Day 348 list); `shared_state_check` stays clean.
- [x] **Docs**: `docs/guide/settings.md` reference rows for the new keys, `docs/reference/` regenerated.
- [x] **Standing rows** (series rules).

**Done:** the settings choose stdout and file devices at run time; the C API keeps its log callback; no device outlives its owner; tests pass
plain, asan and tsan; the review is recorded below.

## Notes (2026-11-20, Linux)

- One setting, `log.console` (auto, plain, color, off), replaces the planned `log.stdout` plus `log.color`; the file keys are `log.file.path`, `.level`,
  `.max_bytes`, `.keep`. The path is at most 511 bytes (the settings text limit). The console mode is applied at attach only when a layer set it.
- **The C API callback stays an observer** (decision recorded here): its contract is that returning true keeps the line off the console, which is what
  the observer does; `engine_destroy` already removes it. No change in `engine_c`.
- The file device is kept by the settings in one process-wide holder whose destructor removes it from the registry first, so shutdown is safe.
- Writing the test found a dangling view (`Value::AsText()` of a temporary) in the first version, caught by ASan.
- The three demos' `listsettings.golden` files gained the five keys.

## Post-implementation code review

See Day 5: the review covered the whole series.
