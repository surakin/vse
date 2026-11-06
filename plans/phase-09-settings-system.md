# Phase 9 — Settings System

**Depends on:** Phase 8 (the file layer: `io::Path`, `io::File`, `io::Directory`, atomic replace, the special folders), Phase 7 (the rendering-settings
model this phase generalizes: `renderer::RenderSettings`, the registry with cost classes, the text format), Phase 2 (jobs, for background saving).
Must complete before the UI phase (Phase 13, whose settings screens bind to it) and before the demos get settings options.
**Goal:** one engine-wide settings system: a registry that every subsystem registers its settings with (the renderer's, later audio, input, UI scale, gameplay),
config-file handling classes that read and write them, layered sources with a defined precedence, and the plumbing that connects them to the command line and
to code that reacts to a change. Today the engine has only the rendering-settings model (Phase 7 Day 104); there is no place settings live on disk, no loader, no
shared registry, and so the demos (`renderer_demo`, `rhi_demo`) deliberately have no `--settings`, `--save-settings` or generic `--set key=value` (filed in `KNOWN-ISSUES.md`, 2026-11-06).
**Definition of done (to be refined into day files before the phase starts):**
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

Day files: **not written yet** (policy: a phase's days are written, as files of specific sub-tasks, and committed *before* the phase starts, after Phase 8 has completed
and reality has corrected this plan). The first day is a design spec.

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
