# Settings

Status: draft (Phase 11, Day 131: keys, values, the registry and typed handles; stores, layers, the service and the command line arrive on Days 132-138).

The settings system is one registry that every module declares its settings in, and handles that read them cheaply. Where values come from (files, the environment, the command line) and how they
are saved arrive later in this phase. Design and reasons: `docs/superpowers/specs/2026-11-07-settings-design.md`.

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
