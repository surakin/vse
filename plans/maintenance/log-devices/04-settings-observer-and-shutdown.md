# Log devices / Day 4: settings, the C API observer as a device, shutdown

**Series:** [log devices](../log-devices.md)
**Depends on:** Days 1 to 3.

## Tasks

- [ ] **Tests first** (`tests/settings/log_device_settings_test.cpp`, tag `[log][settings]`; `tests/engine_library` for the C API case):
  - `log.stdout=false` removes the stdout device, `true` restores it;
  - `log.color=never` makes the console device the plain stdout device, `always` the TTY device, `auto` decides from the terminal (Day 2);
  - `log.file.path` set creates a file device and lines reach the file; cleared, the device is removed and flushed; an unopenable path leaves
    the other devices working and records one `Warn` line;
  - `log.file.level`, `log.file.max_bytes` and `log.file.keep` apply on the next change event (rotation limits take effect without a restart);
  - the C API log callback still receives every line (the observer as before, or as a device) and `engine_destroy` removes its device and
    leaves the registry as it found it (two create/destroy cycles);
  - the C API ABI snapshot is unchanged.
- [ ] **Settings** in `engine/foundation/settings/engine_settings.cpp`: register `log.stdout` (bool, default on), `log.color` (`auto`, `always`, `never`; default `auto`), `log.file.path` (string, empty
  = off), `log.file.level` (the verbosity enum), `log.file.max_bytes`, `log.file.keep`, group "log", `kAdvanced`; `AttachLogSettings` owns
  the file device (a `UniquePtr` in the attachment) and updates it from the listener.
- [ ] **The `engine_c` observer**: keep `SetLogObserver` working; decide in this task, and record it here, whether the C API's callback becomes a
  `LogDevice` (preferred, if the 'handled suppresses stdout' rule can be kept) or stays the observer.
- [ ] **Shutdown**: every device a library registers is removed in its teardown (Phase 23 Day 348 list); `shared_state_check` stays clean.
- [ ] **Docs**: `docs/guide/settings.md` reference rows for the new keys, `docs/reference/` regenerated.
- [ ] **Standing rows** (series rules).

**Done:** the settings choose stdout and file devices at run time; the C API keeps its log callback; no device outlives its owner; tests pass
plain, asan and tsan; the review is recorded below.

## Post-implementation code review

(to be written)
