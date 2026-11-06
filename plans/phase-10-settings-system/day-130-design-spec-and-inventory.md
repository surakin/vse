# Phase 10 / Day 130 — Design Spec, Inventory & Phase Setup

**Phase:** [Phase 10 — Settings System](../phase-10-settings-system.md)
**Depends on:** Phase 9 (standard types and containers) and Phase 8 (the file layer) complete.

No product code except the phase setup. The decisions in the phase file are confirmed or amended by experiment, the settings that exist today are inventoried
exactly, and the phase is wired into the test map.

## Tasks

- [ ] **Phase setup first:** `CURRENT_PHASE` becomes 10; `tests/phases.txt` phase list comment names "10 settings system"; `docs/guide/building-and-testing.md` table gets a row for phase 8 (file handling, with the closing counts) and a placeholder
      row for 10; check that a phase with no test files runs nothing and says so. The `flavors 10 ...` line, the `settings` entry in `docs/reference/modules.txt` and the `engine/foundation/settings` line in `tests/module_map.txt`
      arrive on Day 131 with the first header and test (their gates require the directory and a test to exist, as on Phase 8 Day 115).
- [ ] **Inventory of every setting-like thing that exists**, as a table in the spec (name, where it is read, type, default, who can change it, the planned key in the new system, the port day):
      the 14 built-in rendering settings (`renderer/render_settings.h`, their text keys and cost classes); `log::LoadConfig` (`<Channel>.verbosity/enabled` lines) and the channel table; every `getenv` of the engine
      (`ENGINE_WINDOW_BACKEND`, `ENGINE_RHI_GPU`, `ENGINE_RHI_VALIDATION*`, `ENGINE_RHI_CACHE_DIR`, `ENGINE_JOB_THREADS`, `ENGINE_CRASH_HANDLER`, `ENGINE_APP_NAME`, `RHI_DEMO_FRAMES`, `RENDERER_DEMO_FRAMES`); the command-line flags of both demos
      and the benches (`--backend`, `--gpu`, `--validation`, `--no-vsync`, `--frames`, `--script`, ...); `DeviceCreateInfo` fields that are really settings; `CrashHandlerOptions`; `JobScheduler` config. For each: becomes a setting, stays a
      launch-time flag, or stays an environment variable (with the reason). Find the rest with `grep` for `getenv|argv|--[a-z-]+` and record every hit.
- [ ] **Spike A — the file format.** A throwaway program (any language, committed to the scratchpad only): parse and re-emit a sample config (sections, `key = value`, comments before keys and at the end of a line, blank lines, quoted text with escapes, arrays, a duplicated key,
      an unknown key) while changing two values and adding one; show a text diff that touches only those lines. Decide the grammar, the escape rules, how a repeated key and an unknown key are treated, and whether lists exist (the inventory decides). Record the grammar in the spec.
- [ ] **Spike B — reading a setting in a hot path.** A throwaway C++ program: three designs for `Setting<f32>::Get()` called 100 million times from 1 and 4 threads while a writer thread changes the value 1000 times a second — (1) a cached atomic per setting, (2) a pointer to an immutable snapshot swapped by the writer, (3) a lock;
      record ns/call for each and the staleness each allows. The result decides the threading rule.
- [ ] **Spike C — a store that is not a file.** A throwaway implementation of the draft `ConfigStore` over (a) a map in memory, (b) a fake "registry" whose keys are paths and whose values are typed (no comments, no order, per-key writes, no atomic multi-key commit), (c) a store that fails on the Nth call; confirm the draft
      interface (typed keys and values, capabilities, `Commit`) can express all three without an escape hatch, and record what had to change in the draft. This is the proof the interface is not file-shaped.
- [ ] **The spec** `docs/superpowers/specs/2026-11-NN-settings-design.md`: confirms or amends each design decision in the phase file with the evidence; class sketches (`Key`, `Value`, `SettingDesc`, `Registry`, `Setting<T>`, `ConfigStore`, `Layer`, `Settings`);
      the grammar from Spike A; the threading rule from Spike B; the full operation list with `Fails:`/`Aborts:` contracts; the layer order and which layer is written on save; the migration model; the platform sections outline; the inventory table.
- [ ] **File the questions that stay open** (remote and live-reloaded config, a UI binding layer's needs, secrets, per-machine state such as window positions) in the phase file's notes with the day on which each is decided or the phase it is deferred to.
- [ ] **Documentation:** the spec is linked from `docs/README.md`; the phase file's day list is current; `ctest -L docs` passes.
- [ ] Run `scripts/test-phase.sh --affected` (the new phase has no tests yet; quote the line, which says so).
- [ ] No code review (no product code beyond the setup). Update `STATUS.md`; commit.

**Done:** `CURRENT_PHASE` is 10, the spec and the exact inventory are committed, the three spikes are written up with their numbers, and the design decisions are confirmed or changed in the spec.
