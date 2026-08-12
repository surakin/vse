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

## What this project is

A from-scratch, AAA-capable game engine. Read `PLAN.md` first — it is the
master index and states the mission, headline objectives, and the
non-obvious scope assumptions that shape every phase. Each phase has its
own detailed file under `plans/phase-NN-name.md`, linked from the roadmap
table in `PLAN.md`.

## Hard architectural constraints (from PLAN.md — do not violate casually)

- **Zero third-party dependencies.** No SDL/GLFW/Xlib/libxcb/libwayland-
  client/STL/Boost/etc. The only things outside this project's own code
  are the compiler and the OS/vendor boundary: raw syscalls, X11/Wayland
  wire protocols spoken directly, Win32, Cocoa, and GPU driver entry
  points (Vulkan/DX12/Metal). See `PLAN.md`'s "Assumptions & scope notes"
  for the precise reasoning on each boundary — several of these (Vulkan
  WSI surface creation, Wayland XKB keymaps, Android's Bionic libc) have
  open risks flagged that may force narrow, documented exceptions.
- **No CRT/stdlib.** Custom allocators, containers, string handling, and
  math — no malloc/printf/string.h/C++ STL/exceptions/RTTI.
- **RHI abstraction is mandatory.** Renderer/gameplay code must never call
  Vulkan, DX12, or Metal directly — only through the common RHI interface
  defined in Phase 2. Vulkan is the first backend; DX12/Metal follow later
  behind the same interface.
- **Data-oriented ECS**, not an OOP scene graph.
- **Linux first, Wayland primary / X11 fallback.** Every system is built
  and proven on Linux before porting to Windows/macOS/Android/iOS/consoles
  (Phases 11-14). On Linux, Wayland is the primary display backend (raw
  wire protocol, no libwayland-client); X11 is a fallback backend (raw
  protocol, no libX11/libxcb) for systems that haven't moved to Wayland.
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
