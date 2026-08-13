# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

**No code exists yet.** This repository currently contains only planning
documents (`PLAN.md` and `plans/phase-*.md`). There is no build system, no
source tree, no lint/test tooling — because nothing has been implemented.
Do not invent or assume build/test commands; there are none until Phase 0
work begins. Once a build system exists, this section should be updated
with the real commands.

The user has explicitly stated: **do not write code until the plan is
settled.** Treat `PLAN.md` and the `plans/` directory as the current
source of truth for what "settled" means, and check with the user before
starting implementation if it's ambiguous whether planning is done.
"Settled" means Phase 0 is detailed enough to start — it does not mean
the plan is finished or will stop changing. PLAN.md is explicit that this
is a living document: expect scope, architecture calls, and even phase
boundaries to keep changing throughout the project as implementation
reveals what the plan got wrong, not just during this initial pass.

## What this project is

A from-scratch, AAA-capable game engine. Read `PLAN.md` first — it is the
master index and states the mission, headline objectives, and the
non-obvious scope assumptions that shape every phase. Each phase has its
own detailed file under `plans/phase-NN-name.md`, linked from the roadmap
table in `PLAN.md`.

## Hard architectural constraints (from PLAN.md — do not violate casually)

- **Zero third-party *convenience* dependencies.** No SDL/GLFW/STL/Boost/
  etc. Core OS/distro-provided system libraries are allowed, alongside the
  compiler and the OS/vendor boundary: raw syscalls, libc, the reference
  Wayland client library (`libwayland-client`) and X11 client library
  (`libX11`/`libxcb`), Win32, Cocoa, and GPU driver entry points
  (Vulkan/DX12/Metal). The dividing line is "ships with the OS/distro
  itself" vs. "third-party convenience/abstraction library built on top of
  one" — see `PLAN.md`'s "Assumptions & scope notes" for the precise
  reasoning on each boundary.
- **Foundation layer still hand-rolls its own core.** Custom allocators,
  containers, string handling, and math instead of reaching for
  malloc/printf/string.h/the C++ STL — for control, performance, and as a
  learning exercise, not because linking libc is banned outright. STL,
  exceptions, and RTTI remain unused.
- **RHI abstraction is mandatory.** Renderer/gameplay code must never call
  Vulkan, DX12, or Metal directly — only through the common RHI interface
  defined in Phase 2. Vulkan is the first backend; DX12/Metal follow later
  behind the same interface.
- **Data-oriented ECS**, not an OOP scene graph.
- **Linux first, Wayland primary / X11 fallback.** Every system is built
  and proven on Linux before porting to Windows/macOS/Android/iOS/consoles
  (Phases 11-14). On Linux, Wayland is the primary display backend, X11 is
  a fallback backend for systems that haven't moved to Wayland — both
  built on their reference client libraries (`libwayland-client`,
  `libX11`/`libxcb`) rather than a hand-rolled wire-protocol client.
- **Every target platform is designed in from day one — desktop, mobile,
  and console alike — even though only Linux is implemented before
  Phase 11.** Platform-layer and engine-facing interfaces must stay
  shaped so no later backend (Phases 11-14) ever forces a redesign of
  already-built engine layers — see `PLAN.md` assumption #11. Build order
  still follows the roadmap (Linux → Windows/macOS → Android/iOS →
  consoles); that's not license to design as if mobile/console don't
  exist until their phase starts.
- **Unreal Engine project import is a headline objective**, not a nice-to-
  have (Phases 8 and 10): loading a real, unmodified UE 5.3+ project's
  meshes/textures/materials/levels, and eventually Blueprint gameplay
  logic, directly into this engine. Targets uncooked editor-format
  projects, not shipped/cooked games. The `.uasset` container format is
  treated as public, reverse-engineered knowledge to implement from
  scratch (not as a library dependency), same as implementing a protocol
  from its spec.
- **Engine core before editor.** The editor (Phase 15) is built last,
  using the engine's own renderer/ECS — not a separate GUI toolkit.

## How the plan is structured — follow this when doing planning work

- `PLAN.md` is the index only: mission, principles, architecture layers,
  repo layout, the phase roadmap table, and a progress log. It does not
  contain day-by-day tasks.
- Each `plans/phase-NN-*.md` file owns one phase. Near-term phases (00,
  01) are broken into day-sized tasks, each with a concrete, checkable
  **Done condition** — a task without one isn't finished. Later phases are
  intentionally left at deliverable/goal level ("to be expanded into
  day-by-day tasks when started") rather than speculatively detailed far
  in advance.
- Tasks are checked off (`[x]`) in place as completed. Completed work is
  not reordered or deleted — it's the project's history.
- If a task turns out wrong-sized, blocked, or based on a bad assumption,
  stop and edit the relevant plan file before continuing rather than
  silently drifting from it.
- The intended build order follows the phase numbering in `PLAN.md`'s
  roadmap table (dependencies are also noted at the top of each phase
  file) — don't skip ahead to a later phase's work without checking its
  stated dependencies are actually done.
