# Phase 26 / Day 277 — Settings on macOS: the User Defaults Store

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 276.

`engine/foundation/settings/BACKEND.md` for macOS, the environment snapshot, and the `foundation` aggregate complete on macOS: the end of the foundation
port.

## Tasks

- [ ] **Decide the store** in the spec's terms, with a measurement: an `NSUserDefaults` store (suite named after the application's bundle id, typed values,
      per-key writes, `NSUserDefaultsDidChangeNotification` for the change capability) versus the existing file store writing a plist or the engine's own
      format under `~/Library/Preferences` or `Application Support`. Planner's default: **`NSUserDefaults`** as the platform store (it is what
      `defaults read` and configuration profiles use), the file store for the engine's own settings files as on Linux.
- [ ] **`settings/macos/user_defaults_store.mm`:** `Capabilities` (typed values, per-key writes, notifies; no order, no comments), `Describe` (the suite
      name), `Load` (`synchronize` is deprecated: re-read through `persistentDomainForName:`), `Get`, `Set` (bool, integer, double, string; anything else
      `Invalid`), `Remove`, `Enumerate` (prefix match on the domain's keys), `Commit` (success: per-key), `SetChangeListener` (the notification on a private
      queue, never for this store's own writes: filtered by a write counter, tested); a test suite name unique per test run so tests never touch real
      preferences, removed afterwards with `removePersistentDomainForName:`.
- [ ] **`System` layer:** the bundle's registered defaults (`registerDefaults:` from the embedded `Info.plist`, if any) as the read-only layer; absent for
      tests, which is an empty layer, not an error.
- [ ] **`policy_layers`:** the Linux `settings/linux/policy_layers.cpp` counterpart: managed preferences (`/Library/Managed Preferences/<user>/<suite>.plist`)
      read-only, through `CFPreferencesAppValueIsForced` for the forced flag; documented; tested only for absence.
- [ ] **Environment snapshot:** `EnvironmentSnapshot::Capture` through `os::` (Day 269's `_NSGetEnviron`), names case-sensitive.
- [ ] **Conformance:** `tests/settings/config_store_conformance.h` passes for the user-defaults store (the registry-shaped double is the model for a store
      without order or comments); the whole settings suite passes; `settings_backend_lists` and `settings_cross_syntax_macos` pass; `config_access_check`
      applies (no stray `getenv`).
- [ ] **The `foundation` aggregate:** `foundation_settings -> foundation_debug -> foundation_io -> foundation_base` all exist on macOS; `engine/jobs` now links
      the aggregate (Day 273's temporary branch removed for macOS); `tests/foundation`'s `foundation_tests` (file tests; window tests wait for Day 279).
- [ ] **Foundation milestone run:** `scripts/test-phase.sh --flavor all` over the foundation's and the jobs' phases (0, 2, 7, 9, 10, 11, 12, 16 and 26; one
      invocation per phase if the script takes only one, each line quoted), crash tests included; this is the
      "foundation port done" result named in `STATUS.md`.
- [ ] **Documentation:** `settings/BACKEND.md` "macOS, as built (Day 277)"; `docs/guide/settings.md` macOS section (`defaults read <suite>` to inspect);
      `docgen lint` zero.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the notification listener's lifetime against the store's destruction, filtering our own
      writes, test isolation from the user's real preferences.
- [ ] Update `STATUS.md` (foundation port done), `KNOWN-ISSUES.md`; commit and push.

**Done:** the user-defaults store passes the store conformance suite, the `foundation` aggregate and `engine/jobs` build on macOS, and the foundation
milestone run passes in plain, asan and tsan and both unity modes, quoted in this file and in `STATUS.md`.
