# Render settings

Status: written (Phase 8; shared settings system from Phase 11).

**Audience:** renderer programmers, and anyone who adds a rendering option or a settings screen. Read [Renderer](renderer.md) and [The RHI](rhi.md) ("Lifecycle and reset")
first if the terms below are new.

A game has options the player changes while playing: vertical sync, window size, fullscreen, HDR, graphics quality. Some are cheap (read a new number next frame); some
force the engine to rebuild big things (the swapchain, the pipelines, even the GPU connection). A **render setting** is one such option, declared once with a type, a default
and a **cost class** that says how much a change costs. The engine can then compare "what is running" with "what was requested" and do only the cheapest work that is enough,
without restarting the program. You touch this page when you add an option, build a settings screen, or wonder why a change made the screen flicker.

Two layers exist, and this page covers both. `RenderSettings` (header `engine/renderer/render_settings.h`) is the value type the renderer works with. The engine-wide
settings system (Phase 11, see "Rendering settings in the shared system" below) stores the same options in the user's settings file and applies them through the renderer.
If you only read or change settings from game code, go to the shared-system section.

## At a glance

| I need | Use | Notes |
|---|---|---|
| to know what a change will cost | `DiffSettings(current, wanted, capabilities)` | the cost is the largest class among the changes |
| to drop changes the machine cannot do | `ResolveSettings(current, requested, capabilities)` | puts back values whose capability is missing |
| to add an option | fill a `SettingDesc`, `registry.Register(desc)` at start-up | strings must be literals; max 64 settings |
| to read or write one value | `RenderSettings::SetBool` / `SetInt` / `SetEnum` / ... with the `SettingId` | the default policy clamps out-of-range values |
| to save and load | the shared system (`[renderer]` section), or `SerializeSettings` / `ParseSettings` for schema 1 | unknown key: warning; malformed text: error, nothing applied |
| to apply a change at run time | `settings.Apply(requests, count, &renderer::ApplySharedChange, &renderer)` | ends in `Renderer::ApplySettings` |

## Concepts

- A **setting** has a key (`present_mode`), a type (`Bool`, `Enum`, `Int`, `Float`, `Text`), a default, a range, a **cost class**
  and optionally a capability it needs.
- A **`SettingsRegistry`** lists the settings that exist. `SettingsRegistry::Builtin()` is the process-wide one; it starts with
  the built-in settings and later subsystems register their own at start-up.
- **`RenderSettings`** holds one value per registered setting. It is a plain value (about 5 KiB, no heap): copy it, compare it,
  keep the last applied one next to the requested one.
- **`DiffSettings(from, to, capabilities)`** says what changed and what the cheapest sufficient action is.

### Cost classes

The names refer to renderer objects explained in [The RHI](rhi.md) and [Renderer](renderer.md): the **swapchain** is the set of images shown in the window, a **pipeline**
(PSO) is the compiled bundle of shaders and fixed state for a draw, and the **device** is the connection to the GPU that owns every GPU object. A larger class includes the actions of every smaller one, so the cost of a change set is its largest class.

| Class | Meaning | Built-in examples |
|---|---|---|
| `None` | no GPU work | (none yet; a later setting that only a UI reads) |
| `Dynamic` | the next frame reads a new parameter | `gbuffer_debug_layer`, `render_scale` |
| `Swapchain` | recreate the swapchain and what depends on its size or format | `present_mode`, `width`, `height`, `fullscreen`, `swapchain_format`, `hdr` |
| `PipelineRebuild` | recreate pipelines, material bind groups and passes; the device stays | `shader_quality` |
| `DeviceReset` | destroy and create the device; every device-dependent object is rebuilt | `adapter`, `validation`, `sync_validation`, `frames_in_flight` |

`render_scale` and `shader_quality` are marked `mechanism_only`: they exist to exercise the mechanism and nothing reads them yet.

`backend` takes `null`, `vulkan`, `dx12` or `auto` (the default: Direct3D 12 on Windows, Vulkan elsewhere). It is the one setting
that takes effect only when the program starts (`SettingDesc::restart_required`): there is no run-time switch between graphics APIs,
since a game has no reason to change its API while it runs. A change at run time is not applied: `DiffSettings` lists it in
`SettingsChange::restart` instead of `changed`, `ResolveSettings` puts it back, and `Renderer::ApplySettings` applies the rest of the
request and logs that the backend waits for the next start. Through the settings system (`renderer.backend`, flag `--rhi`) the value is
stored for the next start and marked `kRestartRequired`. A backend the program was not built with, or one that cannot run here, fails the
device creation at start with the reason. `adapter` does switch the GPU at run time, as a `DeviceReset` (for example to WARP with
`adapter = Basic Render` on Windows).

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

- Picking a cost class by what the change *usually* does instead of what it *forces*: a too-small class leaves stale GPU objects, a too-large one stalls the player.
- Passing a string that is not a literal to `SettingDesc` (names and enum choices are borrowed, not copied).
- Persisting `SettingId`s: they depend on registration order. Persist keys.
- Expecting a `backend` change to take effect at run time: it is stored for the next start (`restart` in `SettingsChange`).
- Expecting an unsupported setting (no fullscreen, no HDR) to appear in `changed`: it is counted as unsupported and put back by `ResolveSettings`.
- Registering a setting after a `RenderSettings` exists is allowed (the old object reads the default for it), but registering
  from two threads is not.
- Comparing settings that follow different registries aborts.
- Expecting `SetInt` to refuse an out-of-range value: the default policy clamps and returns `Clamped`; ask for
  `RangePolicy::Reject` when a bad value must be an error.

## Rendering settings in the shared system

`renderer::RegisterSharedSettings(&registry)` (`engine/renderer/shared_settings.h`) declares the 14 built-in settings in a `settings::Registry` as `renderer.backend`, `renderer.adapter`, `renderer.validation`,
`renderer.sync_validation`, `renderer.frames_in_flight`, `renderer.present_mode`, `renderer.width`, `renderer.height`, `renderer.fullscreen`, `renderer.swapchain_format`, `renderer.hdr`, `renderer.shader_quality`,
`renderer.gbuffer_debug_layer` and `renderer.render_scale`, with the types, defaults, ranges, range policies and capabilities above. A setting's **cost class** is its **apply class**: the number
`CostClass` has (`none` 0, `dynamic` 1, `swapchain` 2, `pipeline-rebuild` 3, `device-reset` 4), which a listener reads from the `ChangeEvent` to do the cheapest sufficient thing. `RenderSettings` stays the renderer's own
value type; `RenderSettingsFromSnapshot` builds one from a `settings::Settings` snapshot (to start the renderer with the loaded settings) and `RenderSettingsToChanges` goes the other way.

To change a setting at run time through the shared system, call `settings.Apply(requests, count, &renderer::ApplySharedChange, &renderer)`. The apply function builds the requested `RenderSettings` from the
renderer's current ones plus the changes and calls `Renderer::ApplySettings`; if that fails the renderer has already restored its previous settings and the settings system rolls the value back too.
`settings.SetCapabilityCheck(&renderer::RendererCapability, &renderer)` makes a setting that needs a missing capability (fullscreen, HDR) refuse a non-default value instead of silently not applying it.
A change made while the renderer is inside a frame is queued by the renderer and cannot be rolled back afterwards; if it then fails at the frame boundary, `renderer::SyncSharedFromRenderer(&renderer, &settings)` (call it after `RenderFrame`) makes the settings system hold what the renderer actually runs. The apply function builds the request from the values the settings system holds, so a second change made before the first was applied does not drop it. A renderer started from other settings than the settings system holds (a test rig) should be aligned once with `SyncSharedFromRenderer` before the first change, or the first apply would carry the difference too. The sync leaves a restart-required setting (`renderer.backend`) as the settings system holds it: that is the value for the next start. A text longer than the renderer can hold (63 characters) is refused with `TooLong`, not cut.

In a settings file the keys are the `[renderer]` section:

```
[renderer]
present_mode = "mailbox"
width = 1920
```

The Phase 8 text above (`version = 1` and flat keys) is **schema 1** of the `renderer` section: `renderer::AddRendererMigrations(&settings.Migrations())` renames each flat key to its `renderer.` key when such a file
is loaded, and the file keeps its comments and is rewritten in schema 2 the next time the application saves. `ParseSettings` and `SerializeSettings` are unchanged and still read and write schema 1 for tools that use them.

## See also

[renderer](renderer.md), [rhi](rhi.md), `plans/phase-08-rhi-reset.md`, the design in
`docs/superpowers/specs/2026-11-06-rhi-reset-design.md`.
