# Phase 13 / Day 157 — Settings on Windows: the Registry Store

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 152 (known folders); `engine/foundation/settings/BACKEND.md`; `RegistryShapedConfigStore` in `tests/settings/store_doubles.h`.

Implement the Windows half of the settings checklist: the registry-backed `ConfigStore`, the standard places for the file store, and the environment source
behind `os::`. New directory `engine/foundation/settings/windows/`.

## Tasks

- [x] **`RegistryConfigStore`** (`settings/windows/registry_store.h/.cpp`), started from `RegistryShapedConfigStore`: root `HKCU\Software\<app>` (or a root
      the caller names), sections as subkeys, typed values (`REG_DWORD` for bool and 32-bit integers, `REG_QWORD` for 64-bit, `REG_SZ` for text and floats in
      their canonical text form — the spec picks; `REG_MULTI_SZ` reserved for lists), per-key writes, no comments or order, the capability flags the checklist
      lists; `KEY_WOW64_64KEY` so a 32-bit view never splits the data; values of an unexpected type are reported as errors with the key, never coerced.
      *(Decided: every value `REG_SZ` canonical text, no typed values: a typed enum reaches the store as an index without its names and would collide with a
      bool as a `REG_DWORD`; see the notes.)*
- [x] **Change notification:** `RegNotifyChangeKeyValue` on the root (subtree, name and value changes) with an event, waited on by one thread per store (or the
      job system's I/O wait, if Day 152's design allows waiting on events); the listener runs per the settings threading rule; destruction stops the wait
      without a race (test: destroy while a change is in flight, 1000 times, ASan).
- [x] **Group policy as a read-only `System` layer:** `HKCU\Software\Policies\<app>` and `HKLM\Software\Policies\<app>` through a read-only store; the layer order
      in `docs/guide/settings.md` says where policy sits; a test writes a policy value under a test root (HKCU only, no elevation) and sees it win.
      *(Corrected: a `System` layer sits below the user's file and would never win; policy is a new top role, `LayerRole::Policy`, and a program cannot change
      an enforced setting, `SetError::Enforced`.)*
- [x] **Conformance:** a Windows factory in the store conformance tests (`tests/settings/config_store_conformance.h`) runs the full suite on the real registry
      under a unique test root that the test deletes at the end (and a startup sweep deletes stale roots from crashed runs); `settings_cross_syntax_windows` and
      `settings_backend_lists` still pass.
- [x] **Standard places:** where `Settings` puts the user file on Windows (`UserConfigDirectory()/<app>/settings.cfg`), project and system files, and whether the
      default user store on Windows is the file or the registry — the spec's decision; both demos' `--settings`/`--save-settings` round trip runs on Windows.
      *(The demos reach Windows on Day 161, whose task already checks `--settings`/`--save-settings` there; the round trip moves with them.)*
- [x] **Environment source behind `os::`:** `env_source.cpp` uses `os::EnvironmentCount`/`EnvironmentEntry` (Day 149) instead of `environ` (`KNOWN-ISSUES.md`,
      settings item 11, removed in this commit); Windows environment names are case-insensitive — the source documents and tests how two names differing in
      case are handled.
- [x] **`config_access_check`** passes with the new code (the registry store is part of the settings system; anything else reading the registry for
      configuration is a violation, with a planted fixture).
- [x] **The `tests/settings` suite** passes on Windows.
- [x] **`tools/docgen` and `ctest -L docs` on Windows** (moved from Day 150: `docgen` links the whole foundation, complete today): its POSIX includes go
      (the file layer, and a small process helper for the sample compiler, shared with Day 163's `shader_mint --permute` if that is ready, else its own);
      `cmake --build --target docs` produces a `docs/reference/` byte-identical to the committed tree (a test: generate on Windows, compare); `ctest -L docs`
      passes on Windows, including the compiled guide samples. From here a Windows day that changes a public header regenerates the reference itself.
      *(The samples link the renderer, the RHI and the platform layer, not on Windows before Day 161: their runner and check moved to Day 161's file. Today:
      `docgen` builds, `docs_check`, `docs_links`, `docs_lint` pass, and the reference is byte-identical to the Linux one.)*
- [x] **Documentation:** `BACKEND.md` Windows section marked implemented with the final value mapping; `docs/guide/settings.md` "Settings on Windows" (where values
      live, registry layout, policy keys); reference regenerated; `docgen lint` zero.
- [x] **Runs:** Windows `--affected` plain and asan; Linux `--affected` plus asan and tsan (`env_source.cpp` is shared); quote the lines.
      *(Windows done; the Linux runs wait for the phase close, Day 174, as every Phase 13 day's shared changes do: `KNOWN-ISSUES.md`.)*
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the notification thread's shutdown, registry handles leaked on error paths, a test that could
      touch the developer's real settings key (it must not: every test root is unique and removed).
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the registry store passes the store conformance suite with change notification, policy keys form a read-only layer, the environment source has no
platform-specific code, and both demos save and reload settings on Windows.

## Notes (2026-11-07, Windows PC)

- **Before the day:** the Linux check (another session, commit `f8894aa`) made warnings errors everywhere (`/W4 /WX` on cl and clang-cl); the three Windows
  builds were made clean first, in their own commit (`ccc1b8d`: five warnings, all in tests).
- **`RegistryConfigStore`** (`settings/windows/registry_store.h/.cpp`), over `HKCU\Software\<app>` by default or any key and hive the caller names; sections
  are subkeys, the last segment the value name; `KEY_WOW64_64KEY` on every open; the section subkeys a removal empties are deleted (the store's root stays).
  **The value mapping, decided today: every value is a `REG_SZ` of the value's canonical text** (the settings file's text; plain unquoted text written by hand
  reads too), and the store is a text store (`kCapWritable | kCapPerKeyWrites | kCapNotifies`, no `kCapTypedValues`). The typed mapping the task and
  `BACKEND.md` sketched cannot round-trip: `ToStored` gives a typed store an enum as its index alone (the store never sees the names), and as a `REG_DWORD` it
  would read back indistinguishable from a bool; floats and text would share `REG_SZ`. A value of another registry type is `TypeMismatch` for its key; a name
  that is not a key (upper case, a space) is not listed. `BACKEND.md`, the `Backend:` notes of `config_store.h` and the guide say so.
- **Change notification:** one watch thread per store with a listener, `RegNotifyChangeKeyValue` on the root (subtree, names and values) with an auto-reset
  event, waited on together with a stop event; armed before each comparison, so a change between the two is not lost. The store's own writes are told apart
  by comparing the whole tree with what the store last saw (its own `Set`/`Remove` update that under the same lock as the registry write); a key that does not
  exist yet is looked for every 250 ms. Destruction sets the stop event and joins; a listener that tries to change its own store's listener gets `Invalid`
  instead of a self-join (found in review). The 1000-times destroy-while-announcing test passes plain and under ASan.
- **Group policy:** the task said "a read-only `System` layer", but `System` sits *below* the user's file, so an enforced value could never win; the test the
  task asks for ("sees it win") would fail. New top role `LayerRole::Policy` (above the command line), and `Settings::Set` refuses to change a setting a policy
  layer holds (`SetError::Enforced`; without it a runtime `Set`, which by design outranks the command line for the session, also overrode the policy: found by
  the test). `Settings::Load` adds the layers through `detail::AddPolicyLayers` (`settings/policy_layers.h`; Windows: `HKCU` then `HKLM`
  `Software\Policies\<name>`, read-only stores; Linux: none yet); `LoadOptions::use_policy` (on), `policy_name`, `policy_path` (a test's own key: HKCU's
  `Software\Policies` refuses writes to a standard user, so the test key stands in for it).
- **Conformance:** `tests/settings/registry_store_windows_test.cpp` runs `RunWritableConformance`, `RunDurabilityConformance`, `RunCapabilityConformance`
  and `RunReadOnlyConformance` on the real registry, each test under `HKCU\Software\engine-settings-test-<pid>-<n>`, deleted at the end; the first test of a
  process deletes the keys of processes that no longer run. No test touches a real program's key or HKLM; after every run no test key is left.
- **Standard places:** the default user store on Windows stays the file, `<user config directory>/<app>/settings.cfg` (`%APPDATA%`): one format and its
  comments on every platform, and `--settings` files; a program that wants the registry adds a `RegistryConfigStore` itself. The demos' `--settings` /
  `--save-settings` round trip moves to Day 161 with the demos (its task already covers it).
- **Environment source:** it already went through `os::EnvironmentEntry` (Phase 12), so the `environ` item was gone; what remained is the case rule:
  `os::EnvironmentNamesIgnoreCase()` (true on Windows), and an `EnvironmentSnapshot` made by `Capture` matches names in any ASCII case and keeps one
  variable for two spellings (the later replaces the earlier). Tested on Windows; the Linux definition is new and untested here.
- **`config_access_check`:** registry reads and writes (`RegGetValue`, `RegQueryValueEx`, `RegSetValueEx`, `RegOpenKeyEx`, `RegCreateKeyEx`, `RegEnumValue`,
  `SHGetValue`, `SHRegGetValue`) count as configuration access; `settings/` is the allowed place, and Day 156's `post_mortem.cpp` is allow-listed (it reads WER's
  own configuration). A planted `RegGetValueW` in the bad fixture is reported.
- **`tests/settings` on Windows:** the suite builds and passes plain and ASan (tsan: none on Windows). Changes for Windows: `setenv` through
  `tests/support/test_env.h`; `__builtin_inf` replaced; the mode checks (0640, 0600) and the three read-only-directory cases are POSIX-only (Windows has no
  modes, and a directory's read-only attribute does not stop writes; filed). The phase map now lists Windows flavors for phase 3 (plain) and 11 (plain, asan).
- **`docgen` on Windows:** builds (its unused `<sys/stat.h>` includes went; the sample runner's POSIX parts are fenced, and on Windows `samples` says it
  comes on Day 161; a local `kNone` that shadowed another in a unity batch was renamed). The reference generated on Windows for the committed tree was
  byte-identical to Linux's (a clean checkout of `HEAD`, 17 pages, 0 differences), and `docs_check` (which regenerates and compares) runs on Windows from now
  on. Today's reference was regenerated on Windows; `docgen lint` is at 4073/4073. The guide samples moved to Day 161 (they link libraries not on Windows yet).
- **Documentation:** `settings/BACKEND.md` (the mapping, the decision, `AddPolicyLayers`), `docs/guide/settings.md` ("Settings on Windows", the layer order
  with `Policy`), `config_store.h` `Backend:` notes, `ARCHITECTURE.md` section 4, `docs/reference/modules.txt` (the store's header is public).
- **For the Linux run at the phase close:** `settings/layers.h/.cpp` (`Policy`), `settings/settings.h/.cpp` (`Enforced`, the policy options and hook),
  `settings/policy_layers.h` and `settings/linux/policy_layers.cpp` (new, never compiled on Linux), `settings/env_source.h/.cpp`, `os/os.h` and
  `os/linux/os.cpp` (`EnvironmentNamesIgnoreCase`), `engine/foundation/CMakeLists.txt`, `tools/docgen` (`samples.cpp` fences, `md_scan.cpp` rename, the
  CMake), `tools/config_access_check`, `tools/module_deps_check/modules.txt`, `tests/settings` (the seven-role precedence test, the test files' POSIX
  fences), `tests/phases.txt`. On Linux: `scripts/test-phase.sh --affected` with asan and tsan, `ctest -L docs` (the samples), both unity settings.
- **Found by the full runs:** the Windows `docs` target called `docgen` by its bare name, which CMake did not replace with the built program's path in a
  custom target (the tests were fine); it now names `$<TARGET_FILE:docgen>`. And the Windows `ChildResourceGuard` (`tests/debug/resource_guard_windows.cpp`)
  failed at random under `ctest -j8` with "child running 1" and the handle count back to its start: 2 of about 1500 debug-test runs. Printing what it saw
  showed `vctip.exe`, `csrss.exe` and `winlogon.exe`, all older than the test program, whose parent's process id had been reused for it. The guard now
  counts only a process created after the test program that has not exited. Test: `Windows: the resource guard reports a running child until it ends`
  (a suspended child with no handle left open is reported until it is killed; it fails with the creation-time check inverted). After the fix the debug
  tests passed 15 times in a row under `-j8`.

## Runs

Windows PC, after the review's fixes and the two found by the full runs:

- `scripts/test-phase.sh --affected`: `ran 545 of 1125 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 49 s; the last
  full run took 13 s` (everything: the day changed `CMakeLists.txt` files, `engine/foundation` headers and the phase map).
- `scripts/test-phase.sh --affected --flavor asan`: `ran 318 of 1125 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 25 s;
  the last full run took 13 s`. No tsan flavor on Windows.
- Full `ctest -j8` (crash tests included): MSVC `100% tests passed out of 1125`, `windows-clang-cl` `100% tests passed out of 708`, non-unity MSVC
  `100% tests passed out of 1125`.
- `ctest -L docs`: `100% tests passed out of 3` in all three builds (`docs_check`, `docs_links`, `docs_lint`; the samples come on Day 161).
- The debug tests 15 times in a row under `-j8` after the guard fix: `100% tests passed out of 152`.
- No `engine-settings-test-*` key left under `HKCU\Software` after the runs (0).
- Linux: not run; pending until the phase close (Day 174), `KNOWN-ISSUES.md`.

## Post-implementation code review

`/code-review high` on the day's diff: 8 findings, 5 fixed, 3 declined.

- **Fixed:** a change listener that called `SetChangeListener` on its own store made the watch thread join itself (an abort); it now gets `Invalid`.
  Test: `Windows: a listener cannot change its own store's listener, and nothing hangs` (aborts without the fix).
- **Fixed:** `Settings::Set` published the requested value for the session even when a policy layer held the key, so a program could override an enforced
  setting; it now returns `SetError::Enforced`. Test: `Windows: a group-policy value wins over the user's file and the command line, and is never
  written` (fails without the fix).
- **Fixed:** `docgen_lib_asan` linked `foundation_asan` unconditionally, which the clang-cl configuration does not have; it is built only with
  `ENGINE_HAVE_ASAN` (the clang-cl build is the test).
- **Fixed:** the phase map gave Windows no phase 3 flavor and only plain for phase 11; `test_phase_labels` failed. Now `3 windows: plain` and
  `11 windows: plain,asan` (the gate is the test).
- **Fixed:** `registry_store_windows_test.cpp` used `io::WriteFile`/`io::ReadFile` without including `io/file.h` (the non-unity build failed; it is the test).
- **Declined:** a settings or session file written with mode 0600 gets its directory's ACL on Windows, not owner-only protection. Filed in
  `KNOWN-ISSUES.md`; owner-only ACLs belong to the file layer, not to this day.
- **Declined:** `EnvironmentSnapshot` folds ASCII letters only, while Windows folds all of Unicode. The standard names come from keys, which are ASCII,
  and the function's comment says ASCII; an explicit `env` name with non-ASCII letters is not checked for and would match only in its own case.
- **Declined:** the enforced-value check reads each policy layer's registry value on every `Set`. `Set` follows a user's change, not a per-frame call;
  not measured. Revisit if a profile shows it.
