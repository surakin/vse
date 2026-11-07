# Phase 12 — Module Layering

**Depends on:** Phase 11 (Settings system) complete. Must complete before Phase 13 (Windows port), which builds and tests each layer on Windows as soon as
its backend is ported.
**Goal:** make the module structure `ARCHITECTURE.md` describes true in the code. Today nine parts of the foundation (base, `mem`, `os`, `os/linux`,
`log`, `containers`, `string`, `io`, `debug`) form one dependency cycle through six upward includes, so `foundation` is one library that cannot be built,
tested or ported a layer at a time; the window and input code lives inside it and drags Wayland and X11 into every program, and the demos include a Linux
header. Found while planning the Windows port on 2026-11-07 (`ARCHITECTURE.md`, section 2).
**Definition of done:** the include graph of `engine/`, `game/` and `tools/` has no cycle and no upward edge except the two `ARCHITECTURE.md` assigns to
Phase 13 Day 162 (the RHI backend registry); the foundation is built as one library per layer (`foundation_base`, `foundation_io`, `foundation_debug`,
`foundation_settings`) with `foundation` kept as the aggregate, and the windows and input code is its own `platform` library under `engine/platform/`, the
only one that links the display libraries; a test of a layer links only that layer and the ones below; the demos and tools include no per-platform header;
a `module_deps_check` gate fails on any new upward edge or cycle; behavior is unchanged (every test, golden image and demo run as before); `ARCHITECTURE.md`
section 2 shows the new graph; the full suite passes in every flavor.

**Status: complete (2026-11-07), Days 140-146.** Day numbers continue from Phase 11 (which ended at Day 139). Day 140's spec confirms or amends
the decisions below.

**Where it runs.** All of it is shared code and the build does not configure on Windows until Phase 13, so every day builds and tests on the **Linux
machine** (`--affected` plus the sanitizer flavors on concurrency, lifetime and memory days). The Windows PC is used only to read and edit; nothing in this
phase claims a Windows result.

## Days

140. [Design Spec, Measured Graph & Phase Setup](phase-12-module-layering/day-140-design-spec-and-setup.md)
141. [The `module_deps_check` Gate](phase-12-module-layering/day-141-module-deps-gate.md)
142. [Assert Reports and Thread Registration as Hooks](phase-12-module-layering/day-142-assert-and-thread-hooks.md)
143. [Log Configuration, OS Primitives, Placement and `engine_main`](phase-12-module-layering/day-143-os-primitives-and-placement.md)
144. [The Platform Layer: Windows and Input Leave the Foundation](phase-12-module-layering/day-144-platform-layer.md)
145. [One Library per Layer](phase-12-module-layering/day-145-library-per-layer.md)
146. [Docs, the Measured Graph, Closing Full Run & Wrap-up](phase-12-module-layering/day-146-docs-and-closing-run.md)

## Design decisions made while planning (2026-11-07; Day 140 confirms or amends them in the spec)

1. **The layers are `ARCHITECTURE.md` section 1's.** The libraries follow them: `foundation_base` (layers 0-5: base, OS primitives, memory, assert and log
   core, containers, strings, math, OS services), `foundation_io` (6), `foundation_debug` (7), `foundation_settings` (8), `platform` (9); jobs, RHI and
   renderer keep their libraries. `foundation` stays as an aggregate (an `INTERFACE` library linking all of them) so programs and tests that link it today
   keep working, and only code that should prove its layering links a single layer.
2. **Upward calls become hooks installed from above**, not moved code where the code belongs where it is: the assert *failure report* (stack trace, crash
   context, debugger break) is installed by `foundation_debug` into a report hook in `log`; the thread registration for crash handling is a thread-start
   hook that `io` and `jobs` call and `debug` installs. Before installation the base behavior is complete on its own (an assert prints its message and
   aborts; a thread starts unregistered), and tests prove both states.
3. **`os.h` sits below strings:** paths are passed as `StringView`, which moves to the base layer if needed (it has no allocator), and `EnvironmentEntry`
   writes into a caller-provided buffer instead of a `String<HeapAllocator>`. Every caller is ported.
4. **Windows and input move to `engine/platform/`** (`window.h`, `input.h`, `key_translate`, the backend plan, `PlatformWindowHost` with a neutral header, the
   Wayland and X11 backends and `xkb_keyboard`; the per-platform `FULLSCREEN.md` checklists go with them, `SESSION.md` stays with the OS services). The
   namespace and the include prefix are the spec's decision (keep `os::` to limit churn, or `platform::` to match the directory); test files and labels keep
   their phase (1).
5. **No behavior changes.** Every existing test, golden image, death test and demo run is unchanged; a test changes only where it named a moved header or
   a removed signature. The renumbered files, moved headers and changed signatures are the whole diff.
6. **The gate comes first** (Day 141), with today's violations in its allow list, each naming the day that removes it, so every later day ends by deleting
   entries and the graph cannot regress meanwhile.
7. **The RHI's two rule breaks stay with Phase 13** (Day 162, the backend registry): they need the DX12 backend's shape to design well. They stay in the
   allow list with that day as the reason.

## Notes / open questions

- Whether `log::LoadConfig` (the channel configuration file) is still needed now that Phase 11 made the log channels settings: retire it, or move its
  file reading into `foundation_settings` (Day 143 decides with a check of its callers).
- Whether `engine_main` (the Phase 1 hello-window program) is still worth keeping beside the demos, or is retired (Day 143).
- Build time with one library per layer, measured before and after (Day 145); unity batches stay per target.
- The `docgen` module map (`docs/reference/modules.txt`) follows the moved headers; the generated reference changes only in paths (Day 144).
