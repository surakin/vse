# Phase 9 — Settings System

**Depends on:** Phase 8 (the file layer: `io::Path`, `io::File`, `io::Directory`, atomic replace, the special folders), Phase 7 (the rendering-settings
model this phase generalizes: `renderer::RenderSettings`, the registry with cost classes, the text format), Phase 2 (jobs, for background saving).
Must complete before the UI phase (Phase 13, whose settings screens bind to it) and before the demos get settings options.
**Goal:** one engine-wide settings system: a registry that every subsystem registers its settings with (the renderer's, later audio, input, UI scale, gameplay),
config-file handling classes that read and write them, layered sources with a defined precedence, and the plumbing that connects them to the command line and
to code that reacts to a change. Today the engine has only the rendering-settings model (Phase 7 Day 104); there is no place settings live on disk, no loader, no
shared registry, and so the demos (`renderer_demo`, `rhi_demo`) deliberately have no `--settings`, `--save-settings` or generic `--set key=value` (filed in `KNOWN-ISSUES.md`, 2026-11-06).
**Definition of done:**
a `Settings` registry any module can register typed settings with (bool, integer with range, float with range, enum, string, and lists if the design needs them), each
with a default, a description, an optional cost/apply class (the renderer's `CostClass` becomes one kind of "what happens when this changes"), and a stable name and id;
a **config store interface** (`ConfigStore`, abstract, like `io::File`): the settings system reads and writes through it and never assumes a file. Backends:
the **file-backed** store (`FileConfigStore`: a documented text format with comments, sections, typed values and line-numbered errors, preserving what a human wrote
when saving (comments and order), built on `io::File` and written atomically) is the one built here; the **Windows registry** store (Phase 10), platform stores (macOS
`NSUserDefaults`/plist, Android `SharedPreferences`, console save data), and in-memory, read-only (shipped defaults) and network stores follow the same interface. The
interface speaks typed keys and values (hierarchical names, bool/int/float/string/list), reports capabilities (writable, preserves comments/order, supports
transactions/atomic save, can notify of external changes, case-sensitive names) and errors as values; a conformance suite any store must pass, with an in-memory store
and a failing/read-only double proving the interface, as Phase 8 does for `File`; a **layered** source is a list of stores, so "user = file, machine = registry" is composition, not special cases; **layered sources** with a documented precedence
(built-in defaults < system < user < project < environment < command line) and a way to ask where a value came from; **change notification** and an apply step for
settings that need work when they change (the Phase 7 `ApplySettings` is the first consumer); **versioning and migration** of keys and values across engine versions;
loading at startup and saving on request or exit, with a failed read never losing the user's file; the renderer's registry **migrated onto the shared one** without
changing its behavior or tests; `--settings`, `--save-settings`, `--set key=value` and `--list-settings` in both demos, with `--help` generated from the registry;
guide `docs/guide/settings.md`; the full suite passes in every flavor.

**Status: planned (2026-11-07), not started.** Days 125-134 are written under `plans/phase-09-settings-system/`; `CURRENT_PHASE` changes to 9 in the commit that completes
Day 125's first task. Day numbers continue from Phase 8 (which ended at Day 124). The definition of done above is the goal; the day files hold the sub-tasks, and Day 125's spec
confirms or amends the decisions below with experiments.

## Days

125. [Design Spec, Inventory & Phase Setup](phase-09-settings-system/day-125-design-spec-and-inventory.md)
126. [Keys, Values and the Registry](phase-09-settings-system/day-126-keys-values-registry.md)
127. [The `ConfigStore` Interface](phase-09-settings-system/day-127-config-store.md)
128. [The Text Format and `FileConfigStore`](phase-09-settings-system/day-128-text-format-file-store.md)
129. [Layered Sources, Environment and Command Line](phase-09-settings-system/day-129-layers-and-sources.md)
130. [The Settings Service: Loading, Change Notification, Saving, Threads](phase-09-settings-system/day-130-settings-service.md)
131. [Versioning, Migration and Unknown Keys](phase-09-settings-system/day-131-versioning-migration.md)
132. [Move the Renderer onto the Shared System](phase-09-settings-system/day-132-renderer-migration.md)
133. [Command Line, the Demos and the Other Consumers](phase-09-settings-system/day-133-command-line-demos-and-config.md)
134. [Platform Checklists, Guide, Closing Full Run & Wrap-up](phase-09-settings-system/day-134-platform-docs-and-closing-run.md)

Order reasoning: Day 125 settles the model and takes the inventory of what must move, because the store interface shape and the read cost of a setting decide everything after it. The model (126) is
pure; the store interface (127) is proven with doubles before any file code exists, so the file store (128) is one implementation of something already shaped by a non-file store. Layers (129) and the
service (130) build on stores; migration (131) needs the service's load path. The renderer (132) is the first real consumer and the strictest test of "no behavior change"; the demos and the scattered
environment variables (133) are the user-visible end. Day 134 adds the platform checklists and a configuration check that keeps new ad hoc configuration from creeping back, and closes the phase.

## Design decisions made while planning (2026-11-07; Day 125 confirms or amends them in the spec)

1. **Where:** `engine/foundation/settings/` (namespace `engine_placeholder::settings`), foundation level, built on `io`. The renderer, the log, the RHI and the demos are clients.
2. **Keys are dotted names** (`renderer.present_mode`, at most 3 segments, lowercase letters, digits and `_`); the file format's `[section]` is the key's prefix. Existing flat keys get the `renderer.` prefix now, while no saved file exists to break.
3. **One registry** (`settings::Registry`), filled at start-up and then read-only. The renderer's `SettingsRegistry` becomes a client (its `CostClass` is one *apply class* table the owner registers); `RenderSettings` stays the renderer's resolved-value struct.
4. **`ConfigStore` is an abstract class**, shaped by Spike C so a file (whole-document, comments, order, atomic), a registry (per-key, typed, unordered) and a failing store all fit: typed keys and values, capabilities, `Commit`, errors as values. The file store, an in-memory store, a read-only store and test doubles are built; platform stores are Phases 10, 11, 19, 20.
5. **Layers** (lowest to highest): defaults (the registry) < system < user < project < environment < command line, each a `ConfigStore`; every resolved value can report its provenance; saving writes only the write layer (the user's), never a `--set`.
6. **Reading a setting must be cheap**: a `Setting<T>` handle reads a published immutable snapshot; Spike B picks the exact mechanism. Writers publish a new snapshot and then call listeners.
7. **A damaged file is never overwritten by accident**: a parse with errors marks the store damaged, `Commit` refuses unless told to replace it, and the previous good file is kept as `.bak`.
8. **Format** (file store only): sections, `key = value`, `#`/`;` comments, quoted text, hand-written parser, comments and order preserved by a line-based document model.
9. **Versioning**: format version in the file, schema version per section, migrations (rename, value map, split/merge, drop) applied on load, unknown keys kept verbatim and reported, newer files read in compatibility mode and never downgraded.
10. **Threads**: any thread reads; changes are made through `Settings` on any thread but listeners run on the thread that changed; consumers that need an owner thread (the renderer) queue the change themselves, as `ApplySettings` already does.
11. **Configuration stays in one place afterwards**: a check (Day 134) fails on new ad hoc `getenv`/flag parsing for configuration in `engine/` and `game/`.

## Key deliverables (the day files hold the sub-tasks)

- **Model:** `Key`, `Value`, `SettingDesc`, `Registry`, `Setting<T>`.
- **Stores:** `ConfigStore`, memory, read-only and file stores, doubles, a conformance suite any store must pass.
- **Layers and sources:** `LayerStack`, environment and command-line sources, provenance.
- **Service:** `Settings` (load, resolve, publish, change, listen, apply, save), versioning and migration.
- **Consumers:** the renderer, the log, the engine's environment variables, both demos (`--settings`, `--save-settings`, `--set`, `--list-settings`, generated `--help`).
- **Platform checklists** (`BACKEND.md`) with a list test and a cross-target syntax check; **docs:** `docs/guide/settings.md`; **a configuration check**.

## Notes / open questions (for the design spec, the phase's first day)

- **Store interface shape.** Read-all-then-write versus per-key get/set with a `Commit()`; whether comments/order preservation is a capability of the file store or part of the contract (the registry has neither); how a store reports external changes (poll, callback, watcher); the Windows registry mapping of sections and lists (Phase 10 implements it; this phase writes its `BACKEND.md` section and a cross-syntax check, as Phases 6-8 do).
- **Format.** (File-backed store only.) A small INI/TOML-like text format (sections, `key = value`, comments, arrays) versus extending the Phase 7 `key = value` format; hand-written parser (no third-party
  dependency in the runtime, `PLAN.md` assumptions #2 and #19). Round-trip with comments preserved is a requirement for files a human edits.
- **One registry or several.** The renderer's `SettingsRegistry` is open and `mechanism_only`-aware; decide whether it becomes the engine registry or a client of a new one, and how
  the 14 built-in rendering settings, their ids and the text keys survive (existing saved files are none yet, so a clean rename is possible now and not later).
- **Where files live.** Per-user config (`UserConfigDirectory`), per-project settings next to the content, a system-wide default; which are writable; how a game ships defaults.
- **Who owns saving.** Explicit `Save()` versus automatic on change; atomic write and a `.bak` of the previous file; what happens on a read-only home directory.
- **Typed access.** Strongly typed handles (`Setting<f32>`) registered at startup versus string lookups; cost of a read in a hot path (a cached handle must be as cheap as a member read).
- **Threads.** Readers on worker threads while the main thread applies a change; snapshot versus lock; the renderer applies on the owner thread at a frame boundary today.
- **Remote and live-reloaded config** (via the `io::File` network backends and a file watcher): out of scope here unless Phase 8's notes pull it in; the design must not preclude it.
- **Secrets and per-machine state** (window positions, last-used GPU): where they go and whether they are "settings" at all.
- **UI binding.** What metadata (labels, ranges, groups, restart-required flags) the registry must carry so Phase 13 can build a settings screen without a second description of each setting.
