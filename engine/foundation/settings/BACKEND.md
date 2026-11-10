# Settings: what a platform store implements

The settings system (`engine/foundation/settings/`) reads and writes through the abstract `ConfigStore`, so the Windows registry, `NSUserDefaults`,
Android `SharedPreferences`, iOS defaults and console save data can each be a store without changing the registry, the layers, the service or any
caller. This list is kept in step with the headers by the test `settings_backend_lists`: every function whose documentation carries a `Backend:`
label has a `###` section here, and every section names such a function. The neutral headers compile for the Windows and macOS targets
(`settings_cross_syntax_windows`, `settings_cross_syntax_macos`). Design and reasons: `docs/superpowers/specs/2026-11-07-settings-design.md`.

A new platform adds `engine/foundation/settings/<platform>/` and selects it in `engine/foundation/CMakeLists.txt` next to the Linux sources (the file
store, the memory stores, the environment and command-line layers and everything else in this directory are portable and need no port).

## Rules every store follows

- **Pass the conformance suite.** `tests/settings/config_store_conformance.h` (`RunWritableConformance`, `RunDurabilityConformance`,
  `RunCapabilityConformance`, and `RunReadOnlyConformance` for a store that cannot be written) takes a factory returning a new instance over the same
  backing each call. A store is not done until all of it passes and `Capabilities()` says only what is true. `tests/settings/store_doubles.h` has
  the doubles to run it through as well: a store that fails the Nth call, one that runs every call on another thread after a delay, and one that
  enumerates in a different order each time. `RegistryShapedStore` (same file) is the model of a registry store: typed values, each write durable at
  once, no order, change notifications from another thread; it passes the whole suite, so the Windows port starts from a passing model.
- **Errors are values.** Map the platform's codes to `StoreError`; never throw, never abort on a bad key or value. `Damaged` is for content that cannot
  be read as a whole; `PermissionDenied` and `Io` for the medium.
- **Keys are `settings::Key`s** (lower case, at most three segments). Reject anything else with `Invalid`. Map the dotted key to the medium's own
  hierarchy (a section is a subkey on Windows); map it back exactly in `Enumerate`.
- **Capabilities are promises.** Set `kCapPerKeyWrites` only if `Set` is durable when it returns (then `Commit` has nothing to do); set
  `kCapAtomicCommit` only if a failed `Commit` leaves the durable state as it was; set `kCapPreservesOrder` only if `Enumerate` returns a stable
  order; set `kCapTypedValues` only if `Get` returns the type that `Set` was given; set `kCapNotifies` only if `SetChangeListener` works.
- **Text and types.** A store without types holds the canonical text (`ToStored(desc, value, false, &out)`); a typed store keeps `Value`s. Neither needs
  the registry: `FromStored` converts and validates against the declaration.
- **Local changes do not notify the writer.** A change listener is told about other writers' changes; it may be called on any thread.

## Capabilities a registry-shaped store needs (what the conformance suite checks per flag)

| Flag | A registry store | What the suite does differently |
|---|---|---|
| `kCapWritable` | set | runs the write suite; without it only `RunReadOnlyConformance` applies |
| `kCapPerKeyWrites` | set | a second instance sees every `Set` before `Commit`; `Load` does not discard writes |
| `kCapAtomicCommit` | not set | nothing is asserted about a failed `Commit`; `Status::done` and `total` report how far it got |
| `kCapPreservesOrder` | not set | `Enumerate` order is not compared |
| `kCapTypedValues` | not set (Windows, Day 157: see below) | `Get` must return a typed value of the type that was stored |
| `kCapNotifies` | set | `SetChangeListener` must succeed (without it, `Unsupported`) |

## Platform notes

- **Windows (Phase 13), built on Day 157:** `RegistryConfigStore` (`settings/windows/registry_store.h`), by default over `HKCU\Software\<app>`.
  Dotted keys become subkeys (`renderer.width` is the value `width` under the subkey `renderer`). **Every value is a `REG_SZ` holding the value's
  canonical text**, the text a settings file holds (`true`, `-42`, `0.1`, `fifo`, `"a name"`; plain text typed by hand reads too): the store is a
  text store, not a typed one. The typed mapping first planned (`REG_DWORD` for bools, `REG_QWORD` for ints, `REG_SZ` for text and enum names) cannot
  round-trip: a typed enum reaches the store as an index without its names, and as a `REG_DWORD` it would be indistinguishable from a bool; text
  round-trips every type exactly and `regedit` shows what a file would. A value of another registry type is `TypeMismatch` for its key, never
  converted. No comments, no order, each write immediate (`kCapPerKeyWrites`), the section subkeys a removal empties are deleted; the 64-bit view
  (`KEY_WOW64_64KEY`) from every process; `RegNotifyChangeKeyValue` on the store's own thread for `kCapNotifies`, with the store's own writes told
  apart by comparing against what it last saw (written under the same lock as its writes). Group policy is the top layer, `LayerRole::Policy`
  (not `System`, which sits below the user's file: an enforced value must win): `HKCU` and then `HKLM` `Software\Policies\<app>`, read-only; a setting a
  policy holds cannot be changed by the program (`SetError::Enforced`). The default user store on Windows stays the file
  (`<user config directory>/<app>/settings.cfg`, as on Linux: one format, comments, `--settings` files); a program that wants the registry adds a
  `RegistryConfigStore` layer itself.
- **macOS (Phase 24):** `NSUserDefaults` (a suite named after the application) or a plist file through the file store; `NSUserDefaultsDidChangeNotification`
  for changes; the application bundle's defaults as the read-only `System` layer.
- **Android (Phase 25):** `SharedPreferences`; `apply()` is asynchronous and `commit()` synchronous, so `Commit` calls `commit()` and reports its result;
  `OnSharedPreferenceChangeListener` for notifications.
- **iOS (Phase 25):** `NSUserDefaults` as on macOS.
- **Consoles (Phase 26):** the platform's save-data API behind the same store; mind size limits and commit latency (`Commit` may be slow: the service
  never calls it from a frame).
- **The environment** (`EnvironmentSnapshot::Capture`) is the one other platform-bound piece: Windows reads `GetEnvironmentStringsW` (UTF-16 to UTF-8), macOS
  `_NSGetEnviron`; consoles may return an empty snapshot. Windows variable names ignore case (`os::EnvironmentNamesIgnoreCase`): the snapshot holds
  one variable for two spellings (the later replaces the earlier) and finds it in any case.

## The functions

### `Capabilities`

Return exactly the flags that are true (see the table above). Called often; it must not do I/O.

### `Describe`

A short text for logs: where the store lives (the registry key path, the defaults domain). Cut to the buffer.

### `Load`

Open the medium and read what is durable, discarding anything staged. A medium that does not exist yet loads as empty and succeeds. A per-key store
has nothing staged; `Load` still re-reads, so it picks up other writers' changes.

### `Get`

Return the stored value (typed if `kCapTypedValues`, else its text). `NotFound` for a missing key, `Invalid` for a key that is not a `settings::Key`.
A write made through this store is visible to it at once (read your writes).

### `Set`

Stage the value (a document store) or write it at once (`kCapPerKeyWrites`). `ReadOnly` if the medium refuses writes, `Invalid` for a bad key or a
value of the wrong form (a typed value given to a text store), `TooLong` for what the medium cannot hold.

### `Remove`

Remove the entry; `NotFound` when there is none and nothing changes. A per-key store removes at once.

### `Enumerate`

Append the keys under the prefix (equal to it or below `prefix.`; empty means all). The order is unspecified unless `kCapPreservesOrder`. Return the
keys exactly as `Set` was given them.

### `Commit`

Make staged changes durable; with nothing staged it succeeds. With `kCapAtomicCommit` a failure changes nothing and keeps the changes staged; without
it, report how many landed in `Status::done`. A per-key store returns success. `Damaged` unless `CommitOptions::replace_damaged`.

### `SetChangeListener`

Optional (`kCapNotifies`). Call the function when another writer changes the durable state; best effort, any thread, never for this store's own
writes. A null function removes the listener. Without the capability leave the default (`Unsupported`).

### `Origin`

Optional. Where an entry came from inside the store, for provenance messages (`line 12`); empty when there is nothing to say.

### `IsPresent`

Optional. Whether the backing medium exists (a file not written yet, a registry key not created yet); used for the load report's `Missing`.

### `AddPolicyLayers`

What `Settings::Load` adds as the `Policy` layers (`settings/policy_layers.h`). Windows (Day 157): two read-only `RegistryConfigStore`s,
`HKCU\Software\Policies\<name>` and then `HKLM\Software\Policies\<name>` (the machine's win, as group policy orders them); `LoadOptions::policy_path`
replaces both with one key of the caller's (a test's own HKCU key). Linux: none yet. A policy key that does not exist is an empty layer.

### `Capture`

`EnvironmentSnapshot::Capture`: copy the process environment as UTF-8 names and values. The only environment access of the settings system; everything else
reads the snapshot.
