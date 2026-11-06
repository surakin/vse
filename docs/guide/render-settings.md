# Render settings

Everything a user can change about rendering without restarting the program is a **setting**. This page covers what a
setting is, what changing one costs, how to add one, and the file format. The header is `engine/renderer/render_settings.h`.
(Applying settings to a running renderer arrives on Phase 7 Day 110; this page is about describing and comparing them.)

## Concepts

- A **setting** has a key (`present_mode`), a type (`Bool`, `Enum`, `Int`, `Float`, `Text`), a default, a range, a **cost class**
  and optionally a capability it needs.
- A **`SettingsRegistry`** lists the settings that exist. `SettingsRegistry::Builtin()` is the process-wide one; it starts with
  the built-in settings and later subsystems register their own at start-up.
- **`RenderSettings`** holds one value per registered setting. It is a plain value (about 5 KiB, no heap): copy it, compare it,
  keep the last applied one next to the requested one.
- **`DiffSettings(from, to, capabilities)`** says what changed and what the cheapest sufficient action is.

### Cost classes

A larger class includes the actions of every smaller one, so the cost of a change set is its largest class.

| Class | Meaning | Built-in examples |
|---|---|---|
| `None` | no GPU work | (none yet; a later setting that only a UI reads) |
| `Dynamic` | the next frame reads a new parameter | `gbuffer_debug_layer`, `render_scale` |
| `Swapchain` | recreate the swapchain and what depends on its size or format | `present_mode`, `width`, `height`, `fullscreen`, `swapchain_format`, `hdr` |
| `PipelineRebuild` | recreate pipelines, material bind groups and passes; the device stays | `shader_quality` |
| `DeviceReset` | destroy and create the device; every device-dependent object is rebuilt | `backend`, `adapter`, `validation`, `sync_validation`, `frames_in_flight` |

`render_scale` and `shader_quality` are marked `mechanism_only`: they exist to exercise the mechanism and nothing reads them yet.

### Capabilities

A setting that needs something the device or window may lack (`fullscreen`, `hdr`) names a capability. `DiffSettings` does
not list such a change as a change when the capability is missing; it lists it as **unsupported**, and
`ResolveSettings(current, requested, capabilities)` returns the requested settings with those values put back.

## Usage

```cpp compile standalone run
#include <cstdio>
#include "renderer/render_settings.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

int main() {
    RenderSettings current;
    RenderSettings wanted = current;
    wanted.SetEnum(settings::kPresentMode, 2);  // immediate
    wanted.SetBool(settings::kFullscreen, true);

    Capabilities caps; // this window cannot go fullscreen
    const SettingsChange change = DiffSettings(current, wanted, caps);
    std::printf("cost: %s, changed: %u, unsupported: %u\n", CostClassName(change.cost), change.changed_count, change.unsupported_count);

    caps.Add("fullscreen");
    std::printf("with fullscreen: %s\n", CostClassName(DiffSettings(current, wanted, caps).cost));
    return 0;
}
```
```output
cost: swapchain, changed: 1, unsupported: 1
with fullscreen: swapchain
```

### Adding a setting

Register it once at start-up, before any thread reads the registry. Strings must be literals (they are borrowed).

```cpp compile standalone run
#include <cstdio>
#include "renderer/render_settings.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

int main() {
    SettingsRegistry registry; // a program registers into SettingsRegistry::Builtin() instead
    RegisterBuiltinSettings(&registry);

    SettingDesc shadows;
    shadows.name = "shadow_resolution";
    shadows.type = SettingType::Int;
    shadows.cost = CostClass::PipelineRebuild;
    shadows.default_int = 2048;
    shadows.min_int = 256;
    shadows.max_int = 8192;
    const SettingId id = registry.Register(shadows);

    RenderSettings current(registry);
    RenderSettings wanted(registry);
    wanted.SetInt(id, 4096);
    std::printf("%s\n", CostClassName(DiffSettings(current, wanted, Capabilities()).cost));
    return 0;
}
```
```output
pipeline-rebuild
```

The diff code never lists settings, so nothing else changes. Pick the cost class by what the change *forces*, not by what it
usually does: a setting that changes a shader's output through a permutation is `PipelineRebuild`, one that changes the
instance or adapter is `DeviceReset`.

## The text format

```
# engine render settings
version = 1
backend = vulkan
adapter =
...
```

- One `key = value` per line, in id order; the same settings always produce the same bytes.
- Values: `true`/`false`, an enum's name, a decimal number (floats print with 9 significant digits and read back exactly), or
  the text up to the end of the line (printable ASCII, at most 63 characters, no leading or trailing blanks).
- Blank lines and lines starting with `#` are skipped. `version = N` may appear once; a number above the program's is an
  error, a missing one reads as version 0.
- An unknown key is a **warning** and is skipped. A malformed line, a bad value, a repeated key or a bad version is an
  **error**, and then nothing is applied: `ParseSettings` fills a copy and replaces your settings only when the whole text was
  good. A number outside its range is clamped (or refused, per the setting) and reported as a warning.
- An older file's renamed key is handled by the migration function you pass to `ParseSettings`.

## Rules and contracts

- Settings and registries are thread-compatible. Register before other threads read.
- Ids are small integers in registration order; they are not stable across programs that register different sets. Persist
  keys (the text format does), not ids.
- `Generation()` counts applied changes and is ignored by `==`.
- The registry holds at most 64 settings, a text value 63 characters.

## Common mistakes

- Registering a setting after a `RenderSettings` exists is allowed (the old object reads the default for it), but registering
  from two threads is not.
- Comparing settings that follow different registries aborts.
- Expecting `SetInt` to refuse an out-of-range value: the default policy clamps and returns `Clamped`; ask for
  `RangePolicy::Reject` when a bad value must be an error.

## See also

[renderer](renderer.md), [rhi](rhi.md), `plans/phase-07-rhi-reset.md`, the design in
`docs/superpowers/specs/2026-11-06-rhi-reset-design.md`.
