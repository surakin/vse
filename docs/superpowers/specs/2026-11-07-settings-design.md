# Settings system design (Phase 11)

Status: decided on Phase 11 Day 130 (2026-11-07). Implemented by Days 131-139. Experiments: three spikes whose programs live in the session scratchpad and
whose results are recorded in section 3.

## 1. Decisions of the phase file, confirmed or amended

| # | Decision | Verdict |
|---|---|---|
| 1 | `engine/foundation/settings/`, namespace `vse::settings`, built on `io` | **Confirmed.** Jobs are not needed by the model; background saving is a caller choice (`Settings::Save` is synchronous and takes the write layer's lock). |
| 2 | Dotted keys, at most 3 segments, `[section]` is the prefix; flat keys get the `renderer.` prefix | **Confirmed.** Spike A's sample round-trips with sections. A key segment is `[a-z][a-z0-9_]*`, 1 to 31 characters; the whole key at most 95. |
| 3 | One `settings::Registry`; the renderer's `SettingsRegistry` becomes a client | **Confirmed.** The apply table (`CostClass`) stays the renderer's: the registry carries an opaque `u32 apply_class` and a name table the owner registers. |
| 4 | `ConfigStore` abstract, shaped by Spike C | **Confirmed with four amendments** (section 3, Spike C): a separate `Load`, `Keys(prefix)` for enumeration, `Set` is *staged* on a document store and *immediate* on a per-key store (a capability says which), and `Commit` reports partial failure by naming the first key not written. |
| 5 | Layers defaults < system < user < project < environment < command line; provenance; save writes only the user layer | **Confirmed.** Save writes the *write layer* (default: user); the layer is chosen at `Settings` construction. |
| 6 | Cheap reads | **Amended by Spike B:** a cached relaxed atomic per setting (not a pointer to a snapshot): same cost, no reclamation problem. Multi-setting consistency comes from applying a *change batch* on one thread, not from the read. |
| 7 | A damaged file is never overwritten by accident | **Confirmed.** A parse with errors marks the store `Damaged`; `Commit` returns `Error::Damaged` unless `CommitOptions::replace_damaged` is set, and the previous file is copied to `<name>.bak` first. |
| 8 | Format: sections, `key = value`, `#`/`;` comments, quoted text, hand parser, line-based document model | **Confirmed** (grammar in section 3, Spike A). |
| 9 | Versioning and migration | **Confirmed.** `version = N` is the first key of the root section; section schema versions are stored as `<section>.schema` keys (decided on Day 136). |
| 10 | Threads | **Confirmed with Spike B:** any thread reads (lock-free); `Settings::Set` may be called from any thread, takes one mutex for the batch, publishes, then runs listeners *on the calling thread after releasing the mutex*. |
| 11 | A check keeps ad hoc configuration out | **Confirmed**, Day 139; it extends `tools/std_container_check`'s neighbours (a CMake script gate with an allow list). |

Open questions resolved here: **lists** are not a setting type in this phase (no inventory entry needs one); the file grammar tolerates arrays and keeps them verbatim as
`Unknown` values so a later phase can add the type without a format change. **Secrets** and **per-machine state** (window position, last GPU) are not settings: they get their own
small store when a consumer exists (Phase 15); nothing in this design precludes a second `Settings` instance with a different write layer. **Remote and live-reloaded config**:
`ConfigStore` has an optional `Poll()` returning "changed since the last Load"; no implementation this phase (decided on Day 132, capability `kCapNotifies`). **UI metadata**: `SettingDesc`
carries `label`, `group` and `flags` (`kRestartRequired`, `kAdvanced`, `kHidden`) now, so Phase 15 needs no second description. **Where files live:** user layer =
`io::UserConfigDirectory(app)/settings.cfg`, project layer = `./settings.cfg` next to the content root (read-only by default), system layer = `/etc/<app>/settings.cfg` on Linux (read-only).
**Who saves:** explicit `Save()` only; `Settings` never saves behind the caller's back; a read-only home directory makes `Save` return `PermissionDenied` and leaves memory state intact.

## 2. Inventory of every setting-like thing (2026-11-07)

Found with `grep` for `getenv\(`, `argv` and `"--...`, `log::LoadConfig`, `DeviceCreateInfo`, `CrashHandlerOptions` and `Scheduler::Config`. Verdicts: **setting** (a registry key), **flag** (stays a launch-time
command-line option, not persisted), **env** (stays an environment variable read through the environment layer), **code** (stays a C++ parameter).

### 2.1 The 14 built-in rendering settings (`engine/renderer/render_settings.cpp`)

| Old key | Type | Default | Cost class | New key | Port day |
|---|---|---|---|---|---|
| backend | enum null,vulkan | vulkan | DeviceReset | renderer.backend | 137 |
| adapter | text | "" | DeviceReset | renderer.adapter | 137 |
| validation | enum auto,off,on | auto | DeviceReset | renderer.validation | 137 |
| sync_validation | bool | false | DeviceReset | renderer.sync_validation | 137 |
| frames_in_flight | int 1..3 | 2 | DeviceReset | renderer.frames_in_flight | 137 |
| present_mode | enum fifo,mailbox,immediate | fifo | Swapchain | renderer.present_mode | 137 |
| width | int 0..16384 | 0 | Swapchain | renderer.width | 137 |
| height | int 0..16384 | 0 | Swapchain | renderer.height | 137 |
| fullscreen | bool (capability `fullscreen`) | false | Swapchain | renderer.fullscreen | 137 |
| swapchain_format | enum auto,srgb8,rgba16f,rgb10a2 | auto | Swapchain | renderer.swapchain_format | 137 |
| hdr | enum off,scrgb,hdr10 (capability `hdr`) | off | Swapchain | renderer.hdr | 137 |
| shader_quality | enum low,medium,high | high | PipelineRebuild | renderer.shader_quality | 137 |
| gbuffer_debug_layer | enum none,albedo,normal,material,depth,worldpos,emissive | none | Dynamic | renderer.gbuffer_debug_layer | 137 |
| render_scale | float | 1.0 | Dynamic | renderer.render_scale | 137 |

### 2.2 Environment variables of the engine

| Variable | Read in | Type | Verdict | Planned key / reason | Day |
|---|---|---|---|---|---|
| ENGINE_WINDOW_BACKEND | `os/linux/platform_window_host.cpp:62` | enum wayland,x11 | setting + env | `platform.window_backend`; the env layer maps `ENGINE_WINDOW_BACKEND` | 138 |
| ENGINE_RHI_GPU | `rhi/vulkan/vulkan_device.cpp:206` | text | setting + env | `renderer.adapter` (the same setting) | 137 |
| ENGINE_RHI_VALIDATION | `vk_instance.cpp:231` | bool | setting + env | `renderer.validation` | 137 |
| ENGINE_RHI_VALIDATION_SYNC | `vk_instance.cpp:232` | bool | setting + env | `renderer.sync_validation` | 137 |
| ENGINE_RHI_VALIDATION_BEST_PRACTICES | `vk_instance.cpp:233` | bool | setting + env | `renderer.validation_best_practices` | 137 |
| ENGINE_RHI_VALIDATION_FATAL | `vk_instance.cpp:319` | bool | **env** | test-harness switch read in a layer callback; a process-wide kill switch, not user configuration | stays |
| ENGINE_RHI_CACHE_DIR | `vulkan_device_pipelines.cpp:69` | path | setting + env | `renderer.pipeline_cache_dir` | 137 |
| ENGINE_JOB_THREADS | `jobs/topology.cpp:63` | int | setting + env | `jobs.threads` (0 = automatic) | 138 |
| ENGINE_CRASH_HANDLER | `debug/program_setup.cpp:105` | bool | setting + env | `debug.crash_handler` | 138 |
| ENGINE_APP_NAME | `io/linux/file_system.cpp:419` | text | **env** | chooses the config directory itself, so it must be known before any store loads | stays |
| RHI_DEMO_FRAMES, RENDERER_DEMO_FRAMES | demo `main.cpp` | int | **flag** | `--frames`; the variables stay for the ctest scripts | stays |
| ENGINE_RHI_UPDATE_GOLDEN, ENGINE_X11_TEST_LAYOUT, ENGINE_STRESS_SEED | tests | text | **env** | test harness only | stays |

### 2.3 Other sources

| Thing | Where | Verdict | Planned key |
|---|---|---|---|
| `log::LoadConfig` (`<Channel>.verbosity`, `<Channel>.enabled`) | `foundation/log` | setting | `log.<channel>.verbosity`, `log.<channel>.enabled` (dynamic keys under a registered prefix: the registry supports a *prefix setting* family, Day 131); `LoadConfig` stays as a wrapper over the new files until Day 138 |
| `CrashHandlerOptions` (`dump_directory`, `version`, `post_mortem_dumps`, `enabled`) | `debug/crash_handler.h` | `enabled` and `post_mortem_dumps` setting (`debug.*`); the rest **code** (set by the program) | Day 138 |
| `Scheduler::Config` (`participants`, `pin`) | `jobs/scheduler.h` | `participants` setting `jobs.threads`, `pin` setting `jobs.pin_threads` | Day 138 |
| `DeviceCreateInfo` (`backend`, `validation`, `sync_validation`, `adapter`, `frames_in_flight`, `pipeline_cache`, `descriptor_sets_per_pool`, `pipeline_cache_dir`, `application_name`) | `rhi/rhi.h` | the first five are the renderer settings above; `pipeline_cache` setting `renderer.pipeline_cache`; `descriptor_sets_per_pool` stays **code**; `application_name` **code** | Day 137 |
| `rhi_demo` flags `--frames --screenshot --backend --gpu --list-gpus --no-vsync --validation --inject-device-loss --inject-oom --help` | `game/rhi_demo/main.cpp` | `--backend/--gpu/--no-vsync/--validation` become `--set` shortcuts; `--frames --screenshot --list-gpus --inject-*` **flags** | Day 138 |
| `renderer_demo` flags `--frames --screenshot --backend --no-vsync --validation --objects --lights --gbuffer --inject-* --gpu --prewarm --list-gpus --script --help` | `game/renderer_demo/main.cpp` | as above; `--objects --lights --gbuffer --prewarm --script` are demo scene options, **flags** | Day 138 |
| `rhi_bench` / `renderer_bench` flags (`--section --draws --frames --skip-uploads --skip-pipelines --threads --allow-growth ...`) | `game/*_bench` | **flags** (benchmark parameters) | stays |
| Tool flags (`docgen`, `crash_extract`, `shader_mint`) | `tools/` | **flags**: offline tools, not engine configuration | stays |

## 3. Spikes (programs in the session scratchpad; results here)

### Spike A — the file format

A Python re-emitter over a sample with sections, `key = value`, a comment line above a key, a trailing comment (`#` or `;`) after a value, quoted text with `\"` escapes, an array, a duplicated key and an
unknown key. Changing `height` and `width` and adding `renderer.hdr` produced this diff, and nothing else:

```
-height = 1080            +height = 1440
-width = 1280   ; ...     +width = 2560   ; ...            +hdr = "off"
```

Grammar decided:

- A file is lines. Blank lines and lines whose first non-blank character is `#` or `;` are comments. `[section]` starts a section (`[a]` or `[a.b]`; the key `renderer.width` lives under `[renderer]` as `width`, and a root key has no section).
- `key = value` with blanks around `=` kept as written. A value is `true|false`, an integer (`-12`, `0x1f`), a float (`1.5`, `1e3`), a bare word (`mailbox`), or a double-quoted string with escapes `\"`, `\\`, `\n`, `\t`, `\xHH`; an array `[a, b]` is read as one opaque `Unknown` value. A value ends at a blank followed by `#` or `;` (a trailing comment) or at the end of the line.
- Lines are at most 4096 bytes. Keys outside the segment grammar are errors with a line number; the file store reports up to 32 errors and keeps reading.
- A **repeated key**: the last wins when reading, a warning with both line numbers is reported, both lines are kept on save, and a change edits the last one (the one that wins).
- An **unknown key** (not in the registry) is kept verbatim and reported as `Unknown`; it is never dropped on save.
- Saving replaces only the value text of a changed line (indent, spacing and trailing comment stay), appends a new key after the last key of its section, and appends a new section at the end. A key set back to its default is
  *kept* if it was in the file and *not added* if it was not (so a file does not fill up with defaults).

### Spike B — reading a setting in a hot path

100 million `Get()` calls per reader thread while a writer changes the value every millisecond (clang -O2, this machine):

| Design | 1 reader | 4 readers | Staleness |
|---|---|---|---|
| cached relaxed atomic per setting | 0.86 ns | 0.61 ns | the next read after the store is visible |
| pointer to an immutable snapshot (acquire load) | 0.85 ns | 0.64 ns | same; old snapshots need reclamation |
| mutex | 9.4 ns | 153.8 ns | none, but contended |

(The first two are bound by the dependent float add of the test loop, i.e. a read costs about one load.) **Rule:** `Setting<T>` holds a pointer to the registry's slot and reads a `std::atomic<T>`-like cell with relaxed
order; `Settings::Set` stores the new value into the cell *before* listeners run. A reader that needs several settings to agree reads them inside the apply callback of the owner thread, which runs after the whole batch is published.

### Spike C — a store that is not a file

The draft interface (`Load`, `Get`, `Set`, `Remove`, `Keys(prefix)`, `Commit`, `Capabilities`) was implemented over (a) a map with staged writes, (b) a fake registry with per-key immediate writes and no atomic commit, and (c) a wrapper failing on the Nth call. All three
passed one exercise (set, commit, get, missing key, enumerate) with zero mismatches, and the failing store reported `Io` as a value on exactly the third call. Changes the spike forced on the draft: (1) `Load` is separate from `Get` (a file reads once; a registry opens a handle); (2) enumeration by prefix
is needed (the file store lists a section; the registry lists a subkey path); (3) capabilities `kCapAtomicCommit` and `kCapPerKeyWrites` say whether `Set` is staged or immediate, so a caller that needs all-or-nothing checks the capability; (4) with a per-key store a failure in the middle leaves earlier keys written
(`k1`, `k2` written, `k3` failed), so `Commit`/`Set` return the failing key and `Settings::Save` reports "saved N of M".

## 4. Class sketches

```cpp
namespace vse::settings {
enum class Type : u8 { Bool, Int, Float, Enum, Text };
struct Value { Type type; /* bool/i64/f64/enum index/text */ };
struct SettingDesc { const char* key; Type type; Value def; Value min, max; RangePolicy policy; const char* const* enum_names; u32 enum_count;
                     u32 apply_class; const char* label; const char* group; const char* description; u32 flags; };
class Registry { SettingId Register(const SettingDesc&); const SettingDesc* Find(StringView key) const; /* read-only after Freeze() */ };
template <typename T> class Setting { T Get() const; SettingId Id() const; };           // 1 pointer, relaxed load

class ConfigStore {                                                // abstract
 public:
  virtual u32 Capabilities() const = 0;                            // writable, preserves comments/order, atomic commit, per-key writes, notifies, typed values  (Day 132: no case-sensitivity bit — all keys are lowercase — and no lists)
  virtual io::Status Load() = 0;   virtual io::Status Commit(const CommitOptions&) = 0;
  virtual StoreError Get(StringView key, Value* out) = 0;  virtual StoreError Set(StringView key, const Value&) = 0;  virtual StoreError Remove(StringView key) = 0;
  virtual StoreError Keys(StringView prefix, Vector<Text>* out) = 0;
};
class LayerStack;   // ordered layers {name, ConfigStore*, writable}; Resolve(key, &value, &layer)
class Settings;     // registry + LayerStack + published values; Load, Set(batch), Save, AddListener, Provenance(key)
}
```

Errors are values (`StoreError` = `Ok, NotFound, TypeMismatch, OutOfRange, ReadOnly, Damaged, Io, Unsupported, TooLong`); `Aborts:` only for programming errors (registering after `Freeze`, a key that violates the grammar).

## 5. Operations and contracts (summary; the headers carry the full labels)

- `Registry::Register` — `Aborts:` the registry is frozen, the key is malformed or duplicate, a default is outside its range. `Registry::Freeze` — first `Settings` construction.
- `Settings::Load` — reads every layer; a failed layer is reported and skipped (a missing file is not an error); a damaged user file keeps its raw text and marks the store damaged.
- `Settings::Set(key, value, layer=command)` / `SetBatch` — validates against the descriptor (`Clamp` or `Reject`), publishes, then notifies; `Fails:` `NotFound`, `TypeMismatch`, `OutOfRange`.
- `Settings::Save` — writes the write layer only, atomically (`WriteFileAtomic`), `.bak` first when replacing a damaged file; `Fails:` `Damaged`, `PermissionDenied`, `Io`.
- `Settings::Provenance(key)` — the layer name that supplied the value and, for a file layer, the line number.

## 6. Layers

Defaults (the registry) < system < user < project < environment < command line. The environment layer maps `ENGINE_<SECTION>_<KEY>` (and the legacy names in 2.2) to keys; the command-line layer takes `--set key=value` and the demos' shortcut flags. Only the user layer is written.

## 7. Migration model

`version = N` at the top of every file. A `Migration` is `{from_version, ops}` where an op is `Rename(old, new)`, `MapValue(key, old, new)`, `Split`, `Merge` or `Drop(key)`; they run in order on load over the document model, so comments move with the keys they annotate. Unknown keys survive. A file with a
version newer than the program's is read in compatibility mode (known keys only) and its `version` line is never lowered. Details on Day 136.

## 8. Platform sections

`engine/foundation/settings/BACKEND.md` lists what a platform `ConfigStore` implements: Windows registry (Phase 13: sections are subkeys under `HKCU\Software\<app>`, values typed `REG_DWORD`/`REG_SZ`, capability `kCapPerKeyWrites`), macOS `NSUserDefaults`/plist (Phase 21), Android `SharedPreferences` (Phase 28), console save data (Phase 29). The `settings_backend_lists` ctest and a `settings_cross_syntax_windows|macos` check arrive on Day 139, as for Phases 7-9.
