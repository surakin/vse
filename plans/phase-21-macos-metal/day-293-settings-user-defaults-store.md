# Phase 21 / Day 293 — Settings on macOS: the User Defaults Store

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 292.

`engine/foundation/settings/BACKEND.md` for macOS, the environment snapshot, and the `foundation` aggregate complete on macOS: the end of the foundation
port.

## Tasks

- [x] **Decide the store** in the spec's terms, with a measurement: an `NSUserDefaults` store (suite named after the application's bundle id, typed values,
      per-key writes, `NSUserDefaultsDidChangeNotification` for the change capability) versus the existing file store writing a plist or the engine's own
      format under `~/Library/Preferences` or `Application Support`. Planner's default: **`NSUserDefaults`** as the platform store (it is what
      `defaults read` and configuration profiles use), the file store for the engine's own settings files as on Linux.
      **Decided (Day 293), as Windows did with the registry:** the user's settings stay the file (`~/Library/Application Support/<app>/settings.cfg`:
      one format, comments, `--settings` files everywhere), and `UserDefaultsConfigStore` is the platform store a program adds as its user layer
      when it wants `defaults`; managed preferences are the policy layer. Measured in a throwaway program: 1,000 defaults writes 172 ms
      (0.17 ms each), `synchronize` 0.005 ms, against Day 287's 39 ms for the atomic 4 KiB write a settings-file save makes.
      Also found: `NSUserDefaultsDidChangeNotification` is posted for this process's own writes (and another instance's in the process) but not
      for an outside `defaults write` or a `CFPreferencesSetAppValue`, which the next read still sees.
- [x] **`settings/macos/user_defaults_store.mm`:** `Capabilities` (typed values, per-key writes, notifies; no order, no comments), `Describe` (the suite
      name), `Load` (`synchronize` is deprecated: re-read through `persistentDomainForName:`), `Get`, `Set` (bool, integer, double, string; anything else
      `Invalid`), `Remove`, `Enumerate` (prefix match on the domain's keys), `Commit` (success: per-key), `SetChangeListener` (the notification on a private
      queue, never for this store's own writes: filtered by a write counter, tested); a test suite name unique per test run so tests never touch real
      preferences, removed afterwards with `removePersistentDomainForName:`.
      **Amended (Day 293):** `settings/macos/user_defaults_store.cpp`, through CoreFoundation's `CFPreferences` (the C layer under
      `NSUserDefaults`, same domains and daemon), so no Objective-C. **Text values, not typed** (no `kCapTypedValues`), for the registry store's
      reason: a typed enum reaches a store as an index without its names; a number or boolean written by hand reads as its text. The change
      listener cannot be the notification (it misses other processes): a watch thread compares the domain every 250 ms with what the store last
      saw, its own writes recorded under the same lock (a write counter would not tell its own write from another's landing at the same time).
      Reads ask the domain alone (`CFPreferencesCopyValue`), not the global-domain fallback of `CFPreferencesCopyAppValue` (tested). The test
      suites are emptied key by key with `CFPreferencesSetMultiple`; a crashed run leaves a small `engine-settings-test-*` domain behind.
- [x] **`System` layer:** the bundle's registered defaults (`registerDefaults:` from the embedded `Info.plist`, if any) as the read-only layer; absent for
      tests, which is an empty layer, not an error.
      **Declined (Day 293), with the reason in `settings/BACKEND.md`:** registered defaults are not stored anywhere; a program registers them
      itself at run time (`registerDefaults:`), which makes them the program's own shipped defaults: `LoadOptions::shipped`, a `System` layer
      already. A command-line program has no bundle `Info.plist` to read them from either.
- [x] **`policy_layers`:** the Linux `settings/linux/policy_layers.cpp` counterpart: managed preferences (`/Library/Managed Preferences/<user>/<suite>.plist`)
      read-only, through `CFPreferencesAppValueIsForced` for the forced flag; documented; tested only for absence.
- [x] **Environment snapshot:** `EnvironmentSnapshot::Capture` through `os::` (Day 285's `_NSGetEnviron`), names case-sensitive. (Already
      portable: `Capture` reads `os::EnvironmentEntry`, macOS's since Day 285; a test checks that two spellings stay two variables.)
- [x] **Conformance:** `tests/settings/config_store_conformance.h` passes for the user-defaults store (the registry-shaped double is the model for a store
      without order or comments); the whole settings suite passes; `settings_backend_lists` and `settings_cross_syntax_macos` pass; `config_access_check`
      applies (no stray `getenv`).
- [x] **The `foundation` aggregate:** `foundation_settings -> foundation_debug -> foundation_io -> foundation_base` all exist on macOS; `engine/jobs` now links
      the aggregate (Day 289's temporary branch removed for macOS); `tests/foundation`'s `foundation_tests` (file tests; window tests wait for Day 295).
      (The jobs branch stays, for the flavors, but links `foundation${suffix}` now. `file_io_test.cpp` expected ELF everywhere but Windows: it
      now knows Mach-O, thin or universal.)
- [x] **`docgen` on macOS** (moved from Day 286, which found it needs the whole foundation): build `tools/docgen` (its `child_process_posix.cpp` as on
      Linux); `cmake --build build-macos --target docs` regenerates `docs/reference/` byte-identical to Linux's committed one (compare; fix any ordering,
      path or locale difference); `docs_check`, `docs_links` and `docs_lint` pass on macOS. `docs_samples` waits for Day 311 (the samples link the
      renderer and UI); its target is configured only when those libraries exist.
      (`tools/docgen/CMakeLists.txt` has a macOS branch with the three checks and a `docs` target without samples; `tests/docgen` runs plain
      and asan. One fix: macOS's `<unistd.h>` does not declare `environ` (`child_process_posix.cpp`). **The reference is byte-identical:**
      regenerated on the Mac without today's header, `docs/reference/` differs from Linux's committed one only in the line today's
      `policy_layers.h` comment changes.)
- [x] **Foundation milestone run:** `scripts/test-phase.sh --flavor all` over the foundation's and the jobs' phases (0, 2, 7, 9, 10, 11, 12, 16 and 26; one
      invocation per phase if the script takes only one, each line quoted), crash tests included; this is the
      "foundation port done" result named in `STATUS.md`. (The script takes one phase per call; `--all --flavor all` runs every test the
      Mac builds, which is those phases and the gates and tools besides, crash tests included: quoted in the Result.)
- [x] **Documentation:** `settings/BACKEND.md` "macOS, as built (Day 293)"; `docs/guide/settings.md` macOS section (`defaults read <suite>` to inspect);
      `docgen lint` zero.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the notification listener's lifetime against the store's destruction, filtering our own
      writes, test isolation from the user's real preferences.
- [x] Update `STATUS.md` (foundation port done), `KNOWN-ISSUES.md`; commit and push.

**Done:** the user-defaults store passes the store conformance suite, the `foundation` aggregate and `engine/jobs` build on macOS, and the foundation
milestone run passes in plain, asan and tsan and both unity modes, quoted in this file and in `STATUS.md`.

## Result

Done on 2026-11-12 on the Mac: **the foundation port is done.** Every layer of the foundation (base, file, debugging, settings) and the
`foundation` aggregate build on macOS in three flavors, `engine/jobs` links the aggregate, and docgen builds and regenerates the reference
byte-identical to Linux's. `UserDefaultsConfigStore` passes the store conformance suite and the whole settings suite passes in three flavors.

- **Found on the way:** an outside writer is not announced by macOS (the spike above), so the store watches the domain itself; docgen's
  sample-runner tests compile their samples with the Command Line Tools' compiler by path, which finds no SDK without `-isysroot` (the
  tests get it from `xcrun --show-sdk-path`; CMake leaves `CMAKE_OSX_SYSROOT` empty); `file_io_test.cpp` expected ELF on every system but
  Windows; macOS's `<unistd.h>` does not declare `environ`.
- **Not built, by decision:** the `System` layer from bundle defaults (above). `docs_samples` waits for Day 311, as planned.
- **Observed:** the linker now prints `ignoring duplicate libraries` for test programs that name a foundation layer and also link `jobs`
  (which brings the whole foundation now); a notice of ld64's, not a compile warning, recorded in `KNOWN-ISSUES.md` (the flag that hides it
  would silence a warning, which the rules do not allow).
- **Runs (macOS, the script, crash tests included):** before the review, unity `ran 2108 of 2108 tests (phases: all; flavors: all; display:
  included; crash tests: included) in 303 s`, non-unity the same in 307 s; after it, unity `ran 2108 of 2108 tests (phases: all; flavors: all; display: included; crash tests: included) in 301 s`
  and non-unity the same in 291 s. The failures are the two timing tests recorded
  as flaky under load (`Throughput scales with participant count`, the TSan child timeout), nothing else. `ctest -L docs` 3 of 3.

## Post-implementation code review

`/code-review high` on the day's uncommitted changes: 10 findings; 8 fixed, 1 declined, 1 was the commit's own STATUS update.

1. **Fixed: the forced-only store's `Origin` said `defaults <suite> <key>`**, sending a user to `defaults` for a value a profile enforces. It
   says `managed preferences <suite> <key>`; tested.
2. **Fixed: `CFNumberGetValue`'s result was ignored**, so a number out of a setting's range (an unsigned 64-bit value above `INT64_MAX`)
   would read as a wrong value. It is `TypeMismatch` now. No test: such a number cannot be made with the public CoreFoundation calls.
3. **Fixed: `config_store.h`'s macOS `Backend:` notes described the planned store** (typed values, `objectForKey:`, the notification). Each
   now names what was built; the reference is regenerated.
4. **Fixed: `LoadOptions::use_policy` and `policy_path` said nothing of macOS** (and `policy_path` called itself a registry key).
5. **Fixed: the managed-preferences path was pasted together from the suite**, so a suite with `/` or `..` named a file outside
   `/Library/Managed Preferences`. The suite must be one file name, and the path is built with `io::Path::Join`. The new test passes on the
   old code too (the enforced-value check filtered that file's keys), so it pins the behavior rather than proving the fix.
6. **Fixed: the policy store re-read the managed files on every call**: read once per `Load` (and at each poll), and a `Get` for a key no
   managed file has answers at once.
7. **Fixed: `Enumerate` and `IsPresent` converted every value** just to list keys: a key-only path.
8. **Declined: the watch logic repeats the registry store's** (`RememberOwn`, the comparison, the stop sequence). Sharing it means changing
   the Windows store, which cannot be built or tested here; to be considered when Phase 28 adds a third store.
9. **Fixed: the poll called `CFPreferencesSynchronize` every 250 ms**, the call `Commit` documents as the flush, under the store's lock. It is
   gone: the listener test, which writes with `defaults write` from another process, passed 3 of 3 without it.
10. **`STATUS.md` was not yet in the diff:** updated in this commit.
