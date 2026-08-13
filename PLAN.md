# PLAN.md — Master Plan

## Mission

**Build a general-purpose, AAA-capable game engine from scratch** that
matches and then surpasses the established commercial engines on
capability, performance and platform reach, until choosing it is the
obvious call.

Studios have long treated certain things as the cost of doing business:
stutter that ships because nobody could afford to fix it, shader
compilation that happens on the player's machine, licensing terms that
change under a shipped product. Here they are treated as defects. Shaders
are compiled offline and never at runtime, assets are minted rather than
cooked, and frame-time predictability is a design requirement, not a
post-launch patch.

This is an insanely ambitious goal, and it will probably not be fully
achieved. That is not a valid reason for not even trying: every system
built along the way is worth having on its own, and the attempt is the
point.

The approach: no third-party convenience libraries, hand-written core
systems, built directly on the OS/distro's own system libraries and the
compiler.

## Headline objectives

1. **Truly from scratch.** Zero third-party dependencies. Only the OS/vendor
   boundary (syscalls, X11/Wayland/Win32/Cocoa wire protocols, GPU driver
   entry points) and the compiler itself are external to this project. No
   CRT/stdlib — custom allocators, containers, string, math.
2. **Multi-platform, multi-API.** Windows, Linux, macOS, Android, iOS, Xbox,
   PS5. RHI abstraction over Vulkan, DX12, and Metal as first-class backends.
   Linux + Vulkan is the lead platform during core development.
3. **Unreal project import.** Able to load a real, unmodified Unreal Engine
   project's content — meshes, textures, materials, levels, and eventually
   Blueprint gameplay logic — directly into this engine, without Epic's
   tools or source. This is a headline differentiator, not an afterthought.

This is a solo, AI-assisted effort with no fixed calendar — progress happens
in whatever increments of time/credits are available. The plan is written
as small, resumable, independently-verifiable units of work.

## How this plan is organized

- **This file (PLAN.md)** is the index: mission, principles, architecture,
  the phase roadmap, and a running progress log. It does not contain
  day-by-day tasks.
- **Each phase has its own file under `/plans`** (`plans/phase-NN-name.md`).
  Near-term phases (00, 01 so far) are broken down into day-sized tasks
  with concrete done-conditions. Later phases are scoped at the
  goal/deliverable level and get expanded into day-by-day tasks when the
  prior phase completes and reality has had a chance to correct the plan.
- Every task has a **Done condition** — something concrete and checkable.
  If a task can't be checked, it isn't finished.
- Check off tasks (`[x]`) in the phase files as they complete. Don't reorder
  or delete finished work — it's the project's history.
- When a task turns out wrong-sized or based on a bad assumption, stop and
  edit the plan before continuing rather than silently drifting from it.

## Assumptions & scope notes (please correct if wrong)

Taken to an absolute literal extreme, "zero dependencies" is an infinite
regress (you'd have to fabricate your own transistors). These
interpretations keep it buildable — flag any that don't match intent:

1. **The OS/vendor contract is not a dependency.** Raw syscalls, the ELF/PE
   dynamic loader, X11/Wayland wire protocols, Win32, Cocoa, and the
   Vulkan/DX12/Metal driver entry points are the unavoidable boundary
   between "our code" and "the machine." Using them directly (not through a
   convenience library like SDL/GLFW/Xlib/libxcb) is in scope and required.
2. **No CRT/stdlib means no libc conveniences** (malloc, printf, string.h,
   the C++ STL, exceptions, RTTI). It does **not** mean refusing to let the
   OS's dynamic loader map `libvulkan.so` / `d3d12.dll` / `Metal.framework`
   into our process — that's the same category as a syscall.
3. **Linux window/input targets Wayland as the primary protocol, X11 as a
   fallback backend** — both spoken directly over their Unix domain sockets
   (no libwayland-client, no libX11/libxcb). Wayland is where the Linux
   desktop is heading and X11 is progressively being phased out by
   distributions, so Wayland gets built first; the X11 backend exists so
   the engine still runs on systems/WMs that haven't moved. Both backends
   conform to one internal window/input interface so upper layers never
   know which one is active. Open risk noted in Phase 1/2 plans: Vulkan's
   WSI surface-creation extensions (`VK_KHR_wayland_surface`,
   `VK_KHR_xcb_surface`) are specified against `libwayland-client`/`libxcb`
   object types, since GPU driver WSI implementations call back into those
   libraries internally — a from-scratch client may need to link those
   libraries narrowly just for the surface handle, as a driver-contract
   exception rather than a general convenience dependency. To be resolved
   with a spike at the start of Phase 2.
4. **Shaders/images/assets start custom.** A full GLSL/HLSL compiler or a
   general PNG/JPEG importer is its own large project. Early phases use
   hand-authored SPIR-V and custom binary asset formats from our own
   offline tools. GPU-native compressed texture formats (BCn/ASTC) are
   treated as hardware formats (same boundary as the GPU driver), so
   decoding them ourselves is in scope without counting as "a library."
5. **Unreal import targets uncooked editor-format projects**, i.e. a real
   `Content/` folder from an Unreal *project*, not a shipped/cooked game's
   IoStore chunks — importing a project's authored content is the stated
   objective; reverse-engineering a shipped game's obfuscated runtime
   format is a different (and out of scope) problem.
6. **Unreal's `.uasset` container format is public knowledge, not a
   dependency.** Community projects (UAssetAPI, FModel, CUE4Parse) have
   documented the format through reverse engineering. We build our own
   parser from that documented knowledge without using their code — same
   category as implementing X11 from its protocol spec.
7. **First target CPU/ABI: x86-64 Linux, System V ABI.** ARM and the
   Windows x64 calling convention are handled when those platform phases
   start. **First target Unreal version: UE 5.3+** (property serialization
   has been relatively stable since unversioned properties landed in 4.25).
8. **Build tooling is a dev-time dependency, not a runtime one — CMake now,
   custom later.** "Zero third-party dependencies" governs what ships
   inside the engine/game binaries, not the tool that invokes the compiler
   to produce them (same boundary category as the compiler itself). Phase
   0 starts on CMake so early work isn't blocked reinventing a build
   system. Once the project's build needs (custom toolchain flags,
   cross-compiling to the later platform targets, asset cooking steps) are
   well understood, replace it with our own build tool under `/tools` —
   this is a stated goal, not just an option, tracked as a task once a
   phase for it is scoped.
9. **The engine name is undecided.** A placeholder name is used everywhere
   a name is currently needed (CMake `project()`, namespace, repo-internal
   references) so early work isn't blocked on branding. Keep the
   placeholder isolated to as few, easy-to-grep spots as possible so
   renaming later is a find-and-replace, not a refactor.
10. **Logging must not repeat Unreal's log-spam problem.** The Phase 0
    logging system (Day 9) is designed from the start with named
    channels/categories, independent per-channel verbosity and
    enable/disable, and per-user configuration (overridable at startup
    without recompiling) — not bolted on after the fact once every
    subsystem is already logging unconditionally. Channels default to a
    conservative verbosity rather than "log everything."

## Guiding principles

- **RHI-first rendering.** Gameplay/renderer code never calls Vulkan, DX12,
  or Metal directly — always through a common Render Hardware Interface.
- **Data-oriented ECS**, not an OOP scene graph.
- **Linux first.** Every system is built and proven on Linux before porting.
- **Engine core before editor.** A game running from hand-written code is a
  real milestone; a WYSIWYG editor is a separate, later project.
- **Small, testable subsystems.** Each layer only depends on layers below it
  and exposes a narrow interface.
- **Breadth before depth on Unreal import.** Cover the common cases (common
  material nodes, common Blueprint nodes) and flag unsupported long-tail
  cases explicitly, rather than blocking on 100% fidelity.
- **Quiet by default.** Logging is channel-based with per-channel verbosity
  and per-user configuration from day one — no Unreal-style log spam.

## Architecture layers (dependency flows downward only)

```
Game / Demo content
Editor tooling
Unreal Blueprint import        --> targets Scripting VM
Scripting VM
Unreal asset & level import    --> targets Asset pipeline / ECS / Renderer
Physics | Audio | Asset Pipeline
ECS
Renderer core
RHI  (Vulkan | DX12 | Metal)
Platform layer (window, input, threads, files, time)
Foundation (allocators, containers, string, math, assert)
Raw syscalls / OS ABI
```

## Repository layout

```
/engine
  /foundation   - allocators, containers, string, math, assert, logging
  /platform     - per-OS: linux/, windows/, macos/, android/, ios/
  /rhi          - backend-agnostic interface + vulkan/, dx12/, metal/
  /renderer     - render graph, materials, passes (built on rhi)
  /ecs          - entity/component/system core
  /physics
  /audio
  /asset        - custom formats, offline tools
  /unreal_import - uasset container parser, mesh/texture/material/level
                   importers, blueprint importer
  /scripting
  /editor
/game            - demo/dogfood content, grows into the first playable slice
/tools           - offline asset cookers, custom build tooling
/tests           - per-layer test harnesses (custom test runner, no framework)
/plans           - per-phase detailed plans (this index links to each)
PLAN.md
```

## Phased roadmap

| # | Phase | Platforms | Plan file | Definition of done |
|---|-------|-----------|-----------|---------------------|
| 0 | Foundation layer | none (freestanding) | [plans/phase-00-foundation.md](plans/phase-00-foundation.md) | Custom allocators/containers/string/math pass the custom test harness |
| 1 | Linux platform layer | Linux | [plans/phase-01-linux-platform.md](plans/phase-01-linux-platform.md) | Window opens via raw Wayland protocol (X11 fallback backend also working), handles input/resize/close, stable frame timing |
| 2 | Vulkan RHI | Linux | [plans/phase-02-vulkan-rhi.md](plans/phase-02-vulkan-rhi.md) | RHI renders a lit, textured cube through Vulkan; no Vulkan calls leak outside `/engine/rhi/vulkan` |
| 3 | Renderer core | Linux | [plans/phase-03-renderer-core.md](plans/phase-03-renderer-core.md) | Render graph draws multiple objects, basic materials/lighting, depth buffering |
| 4 | ECS | Linux | [plans/phase-04-ecs.md](plans/phase-04-ecs.md) | Thousands of entities updated per frame, benchmarked vs. naive OOP baseline |
| 5 | Physics | Linux | [plans/phase-05-physics.md](plans/phase-05-physics.md) | Rigid body sim: gravity, collision, resolution for boxes/spheres/capsules |
| 6 | Audio | Linux | [plans/phase-06-audio.md](plans/phase-06-audio.md) | Custom mixer plays hand-decoded WAV, positional 3D audio |
| 7 | Asset pipeline | Linux | [plans/phase-07-asset-pipeline.md](plans/phase-07-asset-pipeline.md) | Offline cooker → engine binary formats, loaded at runtime |
| 8 | **Unreal asset & level import** | Linux | [plans/phase-08-unreal-asset-import.md](plans/phase-08-unreal-asset-import.md) | A real UE5 project's meshes/textures/materials/one level import and render in-engine |
| 9 | Scripting VM | Linux | [plans/phase-09-scripting-vm.md](plans/phase-09-scripting-vm.md) | Custom bytecode VM runs a gameplay script that spawns/moves an entity |
| 10 | **Unreal Blueprint import** | Linux | [plans/phase-10-unreal-blueprint-import.md](plans/phase-10-unreal-blueprint-import.md) | A simple real Blueprint's logic runs correctly via our scripting VM |
| 11 | Windows port + DX12 backend | Windows | [plans/phase-11-windows-dx12.md](plans/phase-11-windows-dx12.md) | Phases 1-10 running on Windows through the DX12 RHI backend |
| 12 | macOS/iOS port + Metal backend | macOS, iOS | [plans/phase-12-macos-ios-metal.md](plans/phase-12-macos-ios-metal.md) | Phases 1-10 running through the Metal RHI backend |
| 13 | Android port | Android | [plans/phase-13-android.md](plans/phase-13-android.md) | Vulkan backend reused; touch input platform layer |
| 14 | Console ports | Xbox, PS5 | [plans/phase-14-consoles.md](plans/phase-14-consoles.md) | Gated on dev kit/NDA access; same RHI, new platform layer |
| 15 | Editor tooling | Linux first | [plans/phase-15-editor.md](plans/phase-15-editor.md) | In-engine editor: scene view, entity inspector, asset browser |
| 16 | First playable demo | Linux, Windows | [plans/phase-16-first-playable-demo.md](plans/phase-16-first-playable-demo.md) | A small real game built with the engine, dogfooding every system |
| 17+ | Feature-parity push | all | [plans/phase-17-feature-parity.md](plans/phase-17-feature-parity.md) | Ongoing: advanced rendering, world streaming, networking — scoped when reached |

## Progress log

_(Append dated entries here as phases/milestones complete, noting anything
that changed from the plan and why.)_
