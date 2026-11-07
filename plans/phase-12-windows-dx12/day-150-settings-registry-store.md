# Phase 12 / Day 150 — Settings on Windows: the Registry Store

**Phase:** [Phase 12 — Windows Port + DX12 Backend](../phase-12-windows-dx12.md)
**Depends on:** Day 145 (known folders); `engine/foundation/settings/BACKEND.md`; `RegistryShapedConfigStore` in `tests/settings/store_doubles.h`.

Implement the Windows half of the settings checklist: the registry-backed `ConfigStore`, the standard places for the file store, and the environment source
behind `os::`. New directory `engine/foundation/settings/windows/`.

## Tasks

- [ ] **`RegistryConfigStore`** (`settings/windows/registry_store.h/.cpp`), started from `RegistryShapedConfigStore`: root `HKCU\Software\<app>` (or a root
      the caller names), sections as subkeys, typed values (`REG_DWORD` for bool and 32-bit integers, `REG_QWORD` for 64-bit, `REG_SZ` for text and floats in
      their canonical text form — the spec picks; `REG_MULTI_SZ` reserved for lists), per-key writes, no comments or order, the capability flags the checklist
      lists; `KEY_WOW64_64KEY` so a 32-bit view never splits the data; values of an unexpected type are reported as errors with the key, never coerced.
- [ ] **Change notification:** `RegNotifyChangeKeyValue` on the root (subtree, name and value changes) with an event, waited on by one thread per store (or the
      job system's I/O wait, if Day 145's design allows waiting on events); the listener runs per the settings threading rule; destruction stops the wait
      without a race (test: destroy while a change is in flight, 1000 times, ASan).
- [ ] **Group policy as a read-only `System` layer:** `HKCU\Software\Policies\<app>` and `HKLM\Software\Policies\<app>` through a read-only store; the layer order
      in `docs/guide/settings.md` says where policy sits; a test writes a policy value under a test root (HKCU only, no elevation) and sees it win.
- [ ] **Conformance:** a Windows factory in the store conformance tests (`tests/settings/config_store_conformance.h`) runs the full suite on the real registry
      under a unique test root that the test deletes at the end (and a startup sweep deletes stale roots from crashed runs); `settings_cross_syntax_windows` and
      `settings_backend_lists` still pass.
- [ ] **Standard places:** where `Settings` puts the user file on Windows (`UserConfigDirectory()/<app>/settings.cfg`), project and system files, and whether the
      default user store on Windows is the file or the registry — the spec's decision; both demos' `--settings`/`--save-settings` round trip runs on Windows.
- [ ] **Environment source behind `os::`:** `env_source.cpp` uses `os::EnvironmentCount`/`EnvironmentEntry` (Day 142) instead of `environ` (`KNOWN-ISSUES.md`,
      settings item 11, removed in this commit); Windows environment names are case-insensitive — the source documents and tests how two names differing in
      case are handled.
- [ ] **`config_access_check`** passes with the new code (the registry store is part of the settings system; anything else reading the registry for
      configuration is a violation, with a planted fixture).
- [ ] **The `tests/settings` suite** passes on Windows.
- [ ] **Documentation:** `BACKEND.md` Windows section marked implemented with the final value mapping; `docs/guide/settings.md` "Settings on Windows" (where values
      live, registry layout, policy keys); reference regenerated; `docgen lint` zero.
- [ ] **Runs:** Windows `--affected` plain and asan; Linux `--affected` plus asan and tsan (`env_source.cpp` is shared); quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the notification thread's shutdown, registry handles leaked on error paths, a test that could
      touch the developer's real settings key (it must not: every test root is unique and removed).
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the registry store passes the store conformance suite with change notification, policy keys form a read-only layer, the environment source has no
platform-specific code, and both demos save and reload settings on Windows.
