# Settings

Status: complete (Phase 11, Days 131-139: keys to the demos, the platform checklist and the configuration check).

The settings system is one registry that every module declares its settings in, and handles that read them cheaply. Where values come from (files, the environment, the command line) and how they
are saved arrive later in this phase. Design and reasons: `docs/superpowers/specs/2026-11-07-settings-design.md`.

## Which API do I use?

| I want to ... | Use |
|---|---|
| declare a setting a user can change | `settings::Make*` (a `SettingDesc`) registered with `Registry::Register`; the registry freezes after setup |
| read a setting on a hot path (per frame, per draw) | the typed handle `Setting<T>` / `TextSetting`: a relaxed atomic read, no lock |
| read many settings that must agree with each other | `Settings::Snapshot()`: one immutable, consistent view |
| change a setting | `Settings::Set` / `SetText`; `Settings::Apply` when the change must take effect on something that can refuse it (a rejected change is rolled back) |
| react when a setting changes | `Settings::AddListener` (with a key prefix), removed with `RemoveListener` |
| know where a value came from | `Settings::Provenance` (layer, file and line, or variable) |
| keep values between runs | `Settings::Save`, `SaveIfChanged`, the `SaveOnExit` guard |
| change a key or its meaning between versions | `MigrationSet` and `Registry::SetSchemaVersion` |
| store settings somewhere new (registry, defaults, save data) | derive from `ConfigStore`, follow `engine/foundation/settings/BACKEND.md`, pass the conformance suite |
| read the environment or parse `--flag` for configuration | do not: declare a setting with `env` and `flag` and the settings system reads them; `config_access_check` fails the build otherwise |

## Keys and values

A **key** is a dotted name such as `renderer.present_mode`: one to three segments, each a lowercase letter followed by lowercase letters, digits and `_` (at most 31 bytes), at most 95 bytes in all.
In a text file `renderer.present_mode` is the name `present_mode` in the section `[renderer]`. `settings::Key::Parse` checks a name and says what is wrong with it.

A **value** (`settings::Value`) is a bool, an `i64` integer, a finite `f64`, an enum index or a text of up to 511 bytes. It is a plain value with no heap memory. Its text form is canonical:
`true`, `42`, `0.75` (a float always has a `.` or an exponent, so it never reads back as an integer), the enum's name, or text in double quotes with the escapes `\"`, `\\`, `\n`, `\t` and `\xHH`.
`settings::ParseValue` reads the text form for a declared setting and `settings::FormatValue` writes it; `NaN`, infinity and numbers with trailing characters are refused.

```cpp compile run
settings::SettingDesc quality = settings::MakeInt("renderer.quality", 2, 0, 3);
settings::Value v;
settings::ValueError e1 = settings::ParseValue(quality, "7", &v);       // outside 0..3: clamped
settings::ValueError e2 = settings::ParseValue(quality, "0x1", &v);
char text[settings::kMaxValueTextBytes];
settings::FormatValue(settings::MakeFloat("a.b", 0, -1, 1), settings::Value::FromFloat(1e-30), text, sizeof(text));
std::printf("%s %s %lld %s\n", settings::ValueErrorName(e1), settings::ValueErrorName(e2), static_cast<long long>(v.AsInt()), text);
```

```output
clamped none 1 1e-30
```

## Declaring a setting

A setting is declared once with a `settings::SettingDesc`: its key, type, default, range (and whether an out-of-range number is clamped or rejected), enum names, and the metadata a settings screen and
`--help` need (`label`, `description`, `group`, the `flags` `kRestartRequired`, `kAdvanced`, `kHidden`), an `apply_class` number the owner of the setting interprets (the renderer's cost class), a
`capability`, an `env` variable name and a `flag` for the command line, and `deprecated_keys` an older release called it. `MakeBool`, `MakeInt`, `MakeFloat`, `MakeEnum` and `MakeText` build the common cases.

A module registers its settings with `settings::Registry::Global()` at start-up (or with a private `Registry` in a test). `Register` returns the `SettingId`, handed out in registration order and never
changed, or an invalid id and a `RegisterError` that says why (a bad or duplicate key, a default outside the range, a bad enum). Registering after `Freeze()` aborts: the registry is filled, then read.
`Find` looks a setting up by key (or by a deprecated key); `SortedAt` iterates in key order.

## Reading a setting

A typed handle reads a setting at the cost of one relaxed atomic load, from any thread: `Setting<bool>`, `Setting<i64>`, `Setting<f32>`, `Setting<f64>`, or `Setting<E>` for an enum type `E`.
Making a handle for the wrong type aborts at creation. A value published with `Registry::Publish` (the settings service does this when a setting changes) is seen by the next `Get`; several
settings read by separate handles can disagree for an instant, so code that needs a consistent set reads them in the callback that runs after a batch of changes. Text is read through `TextSetting`,
which copies under a lock and is for configuration code, not for a hot path.

```cpp compile run
enum class PresentMode { Fifo, Mailbox, Immediate };
const char* const kModes[] = {"fifo", "mailbox", "immediate"};
settings::Registry registry;
settings::SettingId width = registry.Register(settings::MakeInt("renderer.width", 1280, 0, 16384));
settings::SettingId mode = registry.Register(settings::MakeEnum("renderer.present_mode", kModes, 3, 1));
registry.Freeze();

settings::Setting<i64> width_setting(registry, width);
settings::Setting<PresentMode> mode_setting(registry, mode);
std::printf("%lld %d\n", static_cast<long long>(width_setting.Get()), static_cast<int>(mode_setting.Get()));
registry.Publish(width, settings::Value::FromInt(1920));
std::printf("%lld %lld\n", static_cast<long long>(width_setting.Get()), static_cast<long long>(width_setting.Default()));
```

```output
1280 1
1920 1280
```

## Where values live

The settings system never touches a file directly: it reads and writes through a `settings::ConfigStore`, an abstract class like `io::File`. A store holds `key -> value` entries and knows nothing about
the registry, so the same system runs over a text file, the Windows registry, `NSUserDefaults`, Android `SharedPreferences`, console save data, a server, or memory. The stores built in the foundation
are `MemoryConfigStore` (everything the interface offers, in memory), `ReadOnlyConfigStore` (the defaults a game ships, as an array of text entries) and, on Day 133, the file-backed store.

The contract in a few lines:

- `Load()` reads the durable state and discards anything staged. A store whose medium does not exist yet loads as empty.
- `Get`, `Set`, `Remove` and `Enumerate(prefix)` work on the store's *working* state, so a store always reads its own writes. `Enumerate` returns the keys equal to the prefix or below it
  (`renderer` gives `renderer.width`, not `renderers.x`); the order is unspecified unless the store says it preserves order.
- `Set` and `Remove` are **staged** until `Commit()` makes them durable together, except on a store with `kCapPerKeyWrites` (a registry), where each is durable at once. With `kCapAtomicCommit` a failed
  `Commit` changes nothing and leaves the changes staged, so it can be retried; without it `Status::done` says how many landed.
- Errors are values (`Status`, a `StoreError`: `NotFound`, `ReadOnly`, `Damaged`, `Io`, ...). Nothing throws.
- `Capabilities()` says what the store really does: writable, order-preserving, atomic, per-key, notifying, typed. A caller asks instead of guessing, and the conformance suite checks the claims.
- A store may be told to call a function when another writer changes the durable state (`SetChangeListener`, only with `kCapNotifies`); the call is best effort and may come from any thread.

A value goes into a store as a `StoredValue`: typed (`kCapTypedValues`, a medium that records integers as integers, such as `NSUserDefaults`) or its canonical text (a file, and
the Windows registry: see "Settings on Windows"). `ToStored` and `FromStored` are the only place
that converts, so a store never needs the registry's enum names or ranges.

```cpp compile run
settings::MemoryBackingPtr backing = settings::MakeMemoryBacking();   // the "disk": two stores over it see each other's commits
settings::SettingDesc width = settings::MakeInt("renderer.width", 1280, 0, 16384);

settings::MemoryConfigStore writer(backing);
settings::StoredValue stored;
settings::ToStored(width, settings::Value::FromInt(1920), (writer.Capabilities() & settings::kCapTypedValues) != 0, &stored);
writer.Set("renderer.width", stored);                                  // staged
settings::MemoryConfigStore early(backing);                            // a second reader: sees nothing yet
writer.Commit(settings::CommitOptions());                              // now durable
settings::MemoryConfigStore late(backing);

settings::StoredValue got;
settings::Value value;
bool early_has = early.Get("renderer.width", &got).Ok();
late.Get("renderer.width", &got);
settings::FromStored(width, got, &value);
std::printf("%d %lld\n", early_has ? 1 : 0, static_cast<long long>(value.AsInt()));
```

```output
0 1920
```

## Writing a store backend

A new backend (the Windows registry in Phase 13, `NSUserDefaults` in Phase 14) derives from `ConfigStore`, implements the eight pure functions, sets only the capability bits that are true, and passes the
conformance suite in `tests/settings/config_store_conformance.h`: `RunWritableConformance`, `RunDurabilityConformance` and `RunCapabilityConformance` for a writable store, `RunReadOnlyConformance` for one that
is not. Each takes a factory that returns a new instance over the same durable backing every time it is called (a second instance models a restart), so the suite can check that committed content survives
and uncommitted content does not (unless the store is per-key). A store is not ready until the suite passes and its `Capabilities()` match what the suite found.

Three test doubles in `tests/settings/store_doubles.h` show what a backend must not assume, and are worth running any new store through: `FailingConfigStore` fails the Nth call with a chosen error (a failed
`Commit` must leave the durable state as it was), `SlowConfigStore` runs every call on its own thread after a delay (no thread affinity, no immediacy), and `NoOrderConfigStore` enumerates in a different
order each time (a registry has no order). `RegistryShapedConfigStore` goes further and behaves like the Windows registry: typed values, per-key writes, no comments or order, and change notifications from another thread; it passes the whole suite, so a registry-backed store starts from a model that works. `engine/foundation/settings/BACKEND.md` is the checklist per platform (registry, `NSUserDefaults`, `SharedPreferences`, save data) and the ctest `settings_backend_lists` keeps it in step with the headers. Keep the platform's own types out of the interface: the store maps its medium's errors to `StoreError` and its typing to `StoredValue`.

## The file format

`settings::FileConfigStore` keeps settings in a text file a person can edit. A sample:

```
# Engine settings. Comments you write stay where you put them.
version = 1

[renderer]
present_mode = "mailbox"   # fifo | mailbox | immediate
width = 1920
height = 1080
adapter = "NVIDIA \"RTX\" 4090"

[audio]
volume = 0.8
```

- A **comment** is a blank line or a line whose first non-blank character is `#` or `;`; either character also starts a comment after a value, when a blank comes before it.
- `[name]` starts a **section**; the keys below it are `name.<key>` (`[a.b]` makes `a.b.<key>`). Before the first section a key is written whole (`renderer.width = 1`).
- A **value** is a quoted text, `"..."` with the escapes `\"`, `\\`, `\n`, `\t` and `\xHH`, or a bare run of characters up to the end of the line or a blank followed by `#` or `;`. A bare value cannot
  start with `#` or `;`: quote it. Booleans are `true`/`false` (`yes`, `no`, `on`, `off`, `1`, `0` also read), numbers are decimal or `0x` hexadecimal, enums are their names.
- Lines are at most 4096 bytes and the file is UTF-8; a byte order mark and `\r\n` line endings are accepted and kept.
- A key written twice is a **warning**: the last one wins, and a change edits that one.
- A key the program does not know is kept as it is and written back.

A line the parser cannot understand is an **error** with a 1-based line and column: `expected key = value`, `invalid key`, `invalid section name`, `unterminated section header`, `unterminated quoted text`,
`unknown escape in quoted text`, `bad \x escape in quoted text`, `unexpected text after the value`, `unexpected text after the section header`, `line is longer than 4096 bytes`, `a NUL byte in the line`,
`the line is not valid UTF-8`. Parsing goes on after an error and records the first 20.

**What a save changes.** The store edits the document in place: a changed value rewrites only its own bytes (the spacing and the comment after it stay), a new key goes after the last key of its section (a
new section is added at the end, a new top-level key before the first section), and a removed key deletes only its line, never the comments around it. A file that is not changed is written back byte for
byte. `Commit` copies the previous file to `<name>.bak`, then replaces the file atomically (`io::WriteFileAtomic`), so a crash or a full disk leaves the old file whole. A missing file is an empty store; the
first `Commit` creates it with the permissions in `FileStoreOptions` (`0600` for a user's file, `0644` for a project's), and its missing directories only if `create_directories` is set.

**A file that cannot be read is never overwritten by accident.** If any line is an error, `Load` returns `Damaged` and the store is *damaged*: the lines that were readable are available, the rest stay as
they are, and `Commit` refuses with `Damaged` and leaves the file untouched. Only `CommitOptions::replace_damaged` goes ahead, and then the damaged text is in the `.bak` file and the lines that could not be
read are dropped.

```cpp compile run
settings::ConfigDocument doc;
settings::ConfigReport report;
doc.Parse("# mine\n[renderer]\nwidth = 1280   # px\nthis is not a setting\n", &report);

StringView width;
doc.Find("renderer.width", &width);
doc.Set("renderer.width", "1920");
doc.Set("renderer.hdr", "off");
Vector<char> out;
doc.Serialize(&out);
std::printf("%u error: line %u col %u: %s\n", report.error_count, report.errors[0].line, report.errors[0].column, report.errors[0].message);
std::printf("%.*s", static_cast<int>(out.Size()), out.Data());
```

```output
1 error: line 4 col 1: expected key = value
# mine
[renderer]
width = 1920   # px
hdr = off
this is not a setting
```

## Layers and where a value comes from

A program reads its settings from several places, and a `settings::LayerStack` puts them in order. From lowest to highest: the **defaults** declared in the registry, then the layers by role:
`System` (defaults shipped with the program or machine), `User` (the user's file), `Project` (a file next to the content), `Custom` (a file named with `--settings`), `Environment`, `CommandLine`,
and on top `Policy` (values an administrator enforces: Windows group policy; a program cannot change them, `SetError::Enforced`).
A value in a higher layer hides the lower ones. A layer that has no value for a key, cannot be read, or holds a value that does not convert (a bad number in the environment) is skipped for that key, and
the next lower layer answers; each skip is a `LayerProblem` naming the layer, the key and, for a bad value, where it came from.

`Resolve` returns the value and its **provenance**: the index and name of the layer, and what the layer says about the entry (`line 3` for a file, `$ENGINE_RHI_GPU` for the environment, `--set renderer.width=2560` for
the command line). `LoadAll` loads every layer and records its state (`Loaded`, `Missing`, `Damaged`, `Failed`); a failed layer never stops the others.

One layer is *written*, by default the first `User` layer. `Set` puts a value there; with `minimal` a value equal to what the lower layers already give is not written (and an earlier entry is removed), so a
file does not fill up with defaults. `Unset` removes the key so the lower layer shows again. The layers above the write layer are never written.

```cpp compile run
settings::Registry registry;
settings::SettingId width = registry.Register(settings::MakeInt("renderer.width", 1280, 0, 16384));
registry.Freeze();

settings::MemoryBackingPtr shipped = settings::MakeMemoryBacking();
{
    settings::MemoryConfigStore seed(shipped);
    settings::StoredValue v;
    settings::ToStored(*registry.Desc(width), settings::Value::FromInt(1600), true, &v);
    seed.Set("renderer.width", v);
    seed.Commit(settings::CommitOptions());
}
settings::LayerStack stack;
stack.AddLayer("shipped defaults", settings::LayerRole::System, MakeUnique<settings::MemoryConfigStore>(shipped));
stack.AddLayer("user", settings::LayerRole::User, MakeUnique<settings::MemoryConfigStore>());
stack.LoadAll(nullptr);

settings::Resolution r;
stack.Resolve(registry, width, &r, nullptr);
std::printf("%lld from %s\n", static_cast<long long>(r.value.AsInt()), r.layer_name);
stack.Set(registry, width, settings::Value::FromInt(1920), true);
stack.Resolve(registry, width, &r, nullptr);
std::printf("%lld from %s\n", static_cast<long long>(r.value.AsInt()), r.layer_name);
stack.Set(registry, width, settings::Value::FromInt(1600), true); // what the system layer gives: removed, not written
stack.Resolve(registry, width, &r, nullptr);
std::printf("%lld from %s\n", static_cast<long long>(r.value.AsInt()), r.layer_name);
```

```output
1600 from shipped defaults
1920 from user
1600 from shipped defaults
```

## Settings on Windows

**Where values live.** The user's settings are the same file as on Linux, `<user config directory>/<app>/settings.cfg` (`%APPDATA%\<app>\settings.cfg`), so
one format, its comments and `--settings` files work everywhere. A program that would rather keep them in the registry adds a `settings::RegistryConfigStore`
(`settings/windows/registry_store.h`) as its user layer, by default over `HKCU\Software\<app>`.

**The registry layout.** A dotted key's sections are subkeys and its last segment is the value name: `renderer.width` is the value `width` under
`HKCU\Software\<app>\renderer`. Every value is a `REG_SZ` holding the same text a settings file holds (`1920`, `true`, `mailbox`, `"a name"`; plain text without
quotes reads too), so `regedit` shows and edits what a file would. A value of another type (a `REG_DWORD` written by hand) is reported for its key as a type
mismatch and the next layer down answers; it is never converted. The store writes each change at once and tells a listener about other writers' changes (another
process, `regedit`) from a thread of its own. It always uses the 64-bit view of the registry, so a 32-bit tool sees the same values.

**Policy keys.** An administrator enforces a value by writing it under `Software\Policies\<app>`: in `HKCU` for one user, in `HKLM` for the machine (which wins
over the user's policy). `Settings::Load` adds both as read-only `Policy` layers, above everything else (`LoadOptions::use_policy`, on by default;
`policy_name` names another key). A program cannot change a setting a policy holds: `Set` returns `SetError::Enforced`. `--list-settings` names the policy as the
value's source.

**Environment names ignore case** on Windows, as the system's own lookups do: `ENGINE_SETTING_RENDERER__WIDTH` and `engine_setting_renderer__width` are one variable.

## Environment and command line

`settings::EnvironmentSource` makes the process environment a layer. A setting is supplied by `ENGINE_SETTING_<KEY>` with the key in upper case and each `.` written `__`
(`renderer.present_mode` is `ENGINE_SETTING_RENDERER__PRESENT_MODE`), or by the variable its declaration names in `env` (`ENGINE_RHI_GPU`); the standard name wins if both are set.
`EnvironmentSnapshot::Capture()` copies the real environment; a test builds a snapshot by hand.

`settings::CommandLineSource` makes the arguments a layer: `--set key=value` (repeatable; the last one for a key wins; the value may contain `=` and may be quoted), a setting's declared `flag` alias
(`--gpu NAME` or `--gpu=NAME`; for a bool `--vsync`, `--vsync=false` and `--no-vsync`, never a separate value), and the options the application handles itself, recorded in `Result()`: `--settings FILE` (one `Custom` layer per file),
`--list-settings`, `--save-settings[=FILE]` and `--help`. Arguments it does not own stay in `Result().rest`, and a bare `--` ends the options: it and everything after it go to `rest` untouched. A variable that is set but empty counts as unset. An unknown `--set` key is an error that suggests the closest registered key
(`--set renderer.widht: no such setting; did you mean 'renderer.width'?`).

## Loading, changing and saving

An application owns one `settings::Settings` over a registry (it freezes the registry). `Load(LoadOptions)` builds the standard stack for the platform and the program's arguments: the defaults the program
ships (`shipped`), an optional system-wide file, the user's file (`<user config directory>/<app>/settings.cfg`, the layer that is written), an optional project file, the files named by `--settings`,
the environment and the command line. It reads every layer, resolves every setting through the layers and publishes the values. The returned `LoadReport` has one entry per layer (`Loaded`, `Missing`,
`Damaged`, `Failed`) and one `LayerProblem` for everything that was skipped or wrong: a damaged file, a bad value in the environment, an unknown `--set` key with its suggestion. A problem in one
layer never stops the others. `Load` is `LoadFromLayers` after building the stack, and a program or test with its own layers (`Settings::Layers()`) calls `LoadFromLayers` directly.

`Set(id, value)`, `SetText(key, text)` and `Apply(requests, count, apply, user)` change settings. The value is checked against the declaration first: a number outside its range is clamped or refused by the
setting's policy, an enum index must exist, the type must match, and a setting that needs a capability the program does not have (`SetCapabilityCheck`) only accepts its default. The change is written to
the write layer in memory, handed to the apply function if there is one, then published (handles and `Snapshot()` see it), and then the listeners run. Several settings changed by one `Apply` are checked together, published together and announced together;
`Snapshot()` returns an immutable copy of all values taken at one instant, so code that needs several settings to agree reads them from one snapshot rather than through separate handles.

`Save()` writes the user's file: only that layer, only the lines that changed, atomically, with a `.bak` of the previous file. A `--set` value is not saved unless asked (`SaveOptions::include_command_line`, after which they are part of the user's file).
A user file that was damaged on load is not replaced unless the caller says so (`replace_damaged`), and a read-only home directory is a `Status` the application can show: the values in memory are untouched
and `HasUnsavedChanges()` stays true. `SaveIfChanged()` saves only after a change; `SaveOnExit` is a scope guard that does it at the end of `main`; `SetAutosaveQuietSeconds` plus calling `PollAutosave()`
(once a frame is fine) saves after a period without changes, and is off by default. The settings that are not at their default are also written into the crash report (the `settings` entries); a setting declared with `kSecret` shows no value there.

```cpp compile run
settings::Registry registry;
settings::SettingId width = registry.Register(settings::MakeInt("renderer.width", 1280, 0, 16384));
settings::SettingId height = registry.Register(settings::MakeInt("renderer.height", 720, 0, 16384));
settings::Settings settings(registry);
settings.Layers().AddLayer("user", settings::LayerRole::User, MakeUnique<settings::MemoryConfigStore>());
settings.LoadFromLayers();

settings::Setting<i64> width_now(registry, width);
std::printf("%lld\n", static_cast<long long>(width_now.Get()));
settings.Set(width, settings::Value::FromInt(1920));
settings::SetResult too_big = settings.Set(width, settings::Value::FromInt(99999)); // clamped to the range
std::printf("%lld %d\n", static_cast<long long>(width_now.Get()), static_cast<int>(too_big.Ok()));
settings::ChangeRequest both[] = {{width, settings::Value::FromInt(800)}, {height, settings::Value::FromInt(600)}};
settings.Apply(both, 2, nullptr, nullptr);
std::printf("%lld %lld\n", static_cast<long long>(settings.Snapshot()->Get(width).AsInt()), static_cast<long long>(settings.Snapshot()->Get(height).AsInt()));
```

```output
1280
16384 1
800 600
```

## Reacting to a change

`AddListener(prefix, function, user)` subscribes to the settings whose key is the prefix or below it (an empty prefix means all). The function gets a `ChangeEvent`: the changes (old and new value), the
highest `apply_class` among them and whether any is `kRestartRequired`, so a consumer can do the cheapest action that is enough. Listeners run in the order they were added, on the thread that made the change,
after the value is published and with no lock held, so a listener may call `Set` (a nested call is announced too, up to 8 deep; deeper is refused with `SetError::Reentrant`).

`Apply` takes an **apply function** for a consumer that must act on the batch and may refuse it (the renderer's `ApplySettings`). The handles still show the old values while the function runs (the event carries the new ones), because nothing is published until it accepts. If it returns false the write layer is put back exactly as it was, nobody is told, and the call returns `SetError::ApplyFailed`. One lock covers the whole operation, so another thread's change waits until this one has been applied and announced; a listener must not wait for a thread that is itself changing a setting. A consumer that needs an owner thread (a GPU object) queues the change
itself from its listener, as the renderer already does.

## Changing a setting between versions

Settings files outlive the program that wrote them, so a change to a setting is a change to a file format. The rules for the owner of a setting:

- **Never reuse a key for a different meaning.** A key that meant "a width in pixels" does not later mean "a scale". Give the new meaning a new key.
- **To rename a key,** keep the old name as a `deprecated_keys` entry of the new declaration. Files that still use it keep working: the value is read from the old key and the load report says `uses the deprecated key '...'; it is now '...'`.
  When a rename should also rewrite the file, add a `Rename` migration step.
- **To change what a value means,** raise the section's schema version (`registry.SetSchemaVersion("renderer", 3)`) and add a migration step from the version it replaces: `Rename` (move a value to a new key), `MapValue`
  (`fifo` is now `vsync`), `Drop` (the key no longer exists) or `Callback` (a function that may read and write the store, for a split or a merge). Steps are applied on load, in order of `from_version`, to the user's
  file *in memory*: the file is rewritten (comments and all) the next time the application saves, and the load report lists each step it applied. Running a migration twice gives the same result as once.
- **To remove a setting,** add a `Drop` step. An unknown key is not an error: it stays in the file as it is and is mentioned once in the report (with the closest registered key as a suggestion, for a typo).
- **Add the old format as a fixture.** Raising a schema version to N needs the file format of version N-1 in `tests/settings/fixtures/<section>_v<N-1>.cfg`, listed in `SCHEMAS.txt`, loaded by `tests/settings/migration_test.cpp`.
  The ctest `settings_fixtures` fails the build when a `SetSchemaVersion("<section>", N)` in `engine/` or `game/` has no such fixture, no list entry or no test.

A file records the version of the format (`version = 1`) and of each section (`<section>.schema = N`); a new file, and a file that was just migrated, are stamped when saved. A file **newer** than the program
(`version` or a schema above what the program knows) is read in compatibility mode: the settings the program knows are used, everything else stays as it is, the report says so, and `Save` refuses with `TooNew` so an
older program never rewrites a newer file; `SaveOptions::replace_newer` overrides that (the old text goes to the `.bak`).

## Settings in an application

The standard `main` of a program built on the settings system, as `renderer_demo` and `rhi_demo` write it (the helper is `demo::DemoSettings` in `game/common/settings_main.h`):

```cpp
int main(int argc, char** argv) {
    demo::DemoSettings demo_settings;
    if (!demo_settings.Begin("mygame", kUsageText, argc, argv)) return 2;  // registers the settings, loads every layer, prints what went wrong
    argc = demo_settings.RestCount();                                       // the arguments the settings system did not take
    argv = demo_settings.RestArgs();
    // ... the program's own flags, putting the ones that are settings into demo_settings.Get() (SetText("renderer.present_mode", "immediate"))
    const int code = demo_settings.Finish();                                // --help, --list-settings, --save-settings
    if (code >= 0) return code;
    // ... run, reading settings through typed handles, and applying changes with demo_settings.Get().Apply(...)
}
```

Every program built this way understands the same options, and `--help` prints them generated from the registry:

- `--set KEY=VALUE` (repeatable), `--settings FILE`, `--save-settings[=FILE]` (writes the settings that differ from their defaults, so `--settings FILE` reproduces the session), `--list-settings` (every setting: its value, its default,
  the layer and line or variable it came from, and what it is for);
- a flag for each setting that declares one (`--gpu NAME` is `renderer.adapter`, `--backend NAME` is `platform.window_backend`);
- the environment: `ENGINE_SETTING_<KEY>` for any setting, and the variables the engine has always read (`ENGINE_RHI_GPU`, `ENGINE_WINDOW_BACKEND`, `ENGINE_RHI_CACHE_DIR`, `ENGINE_RHI_VALIDATION_SYNC`), which now show up with their
  provenance. Some variables are not settings: `ENGINE_JOB_THREADS`, `ENGINE_CRASH_HANDLER`, `ENGINE_RHI_VALIDATION_BEST_PRACTICES` and the window host read them before any `Settings` exists, and nothing in a settings file changes them (the configuration check lists them as exceptions).

The engine's own settings are `platform.window_backend`, `rhi.pipeline_cache` and `rhi.pipeline_cache_dir`
(`settings::RegisterEngineSettings`), the 14 rendering settings (`renderer::RegisterSharedSettings`), and, for every log channel that exists, `log.<channel>.verbosity` and `log.<channel>.enabled`
(`settings::RegisterLogSettings` and `AttachLogSettings`, which applies them to the channels and keeps them in step). The old `log::LoadConfig` file form (`Channel.verbosity=Info` lines) was retired in Phase 12 Day 143: nothing in the engine called it; use `--set log.<channel>.verbosity=Info`, a settings file or the environment (`ENGINE_SETTING_<KEY>`)..

## Keeping configuration in one place

Anything a user can configure is a setting: declared once, read through `Setting<T>` or `Settings`. The ctest `config_access_check` (`tools/config_access_check`) scans `engine/` and `game/` and fails on a `getenv` call or a
`"--flag"` string outside its allow list (`tools/config_access_check/allowlist.txt`, a reason on every line, a stale entry fails). The exceptions are variables read before any `Settings` can exist (the window host, the job
scheduler, the crash handler, the Vulkan loader and validation switches), the death-test child protocol, and the launch-only options of the demos and benchmarks (frame counts, scenes, fault injection). A new exception needs a
reason that says why the value cannot be a setting.
