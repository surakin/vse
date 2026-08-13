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

1. **From scratch, on core system libraries.** Zero third-party
   *convenience* dependencies — no SDL/GLFW/Boost/C++ STL/etc. What's
   external to this project is the compiler, and the OS/vendor boundary:
   raw syscalls, libc, the reference Wayland/X11 client libraries
   (`libwayland-client`, `libX11`/`libxcb`), Win32, Cocoa, and GPU driver
   entry points. Foundation-layer code (allocators, containers, string,
   math) is still hand-written rather than reached for from libc, for
   control/performance/learning — but linking libc itself is not banned.
2. **Multi-platform, multi-API.** Windows, Linux, macOS, Android, iOS, Xbox,
   PS5. RHI abstraction over Vulkan, DX12, and Metal as first-class backends.
   Linux + Vulkan is the lead platform during core development.
3. **Unreal project import.** Able to load a real, unmodified Unreal Engine
   project's content — meshes, textures, materials, levels, and eventually
   Blueprint gameplay logic — directly into this engine, without Epic's
   editor tools. The offline import tool links a DLL built from Unreal's
   own source to do the parsing (assumption #6) — scoped to that tool,
   never shipped in the engine/game runtime. This is a headline
   differentiator, not an afterthought.

This is a solo, AI-assisted effort with no fixed calendar — progress happens
in whatever increments of time/credits are available. The plan is written
as small, resumable, independently-verifiable units of work.

**This plan is a living document, not a spec to freeze.** A project this
size cannot be fully planned up front — scope, architecture decisions
(e.g. the dependency policy in assumption #1), even phase boundaries will
keep changing as implementation reveals what the earlier planning got
wrong. "Settled," as used in `CLAUDE.md`'s "don't write code until the
plan is settled" rule, means *Phase 0 is detailed enough to start* — not
that every later phase is final or that the plan stops changing once
coding begins. Expect ongoing edits throughout the project's life, not
just during this initial planning pass.

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

1. **The OS/vendor contract, including core system libraries, is not a
   dependency.** Raw syscalls, the ELF/PE dynamic loader, libc, the
   reference Wayland client library (`libwayland-client`) and X11 client
   library (`libX11`/`libxcb`), Win32, Cocoa, and the Vulkan/DX12/Metal
   driver entry points are all in scope — they ship with the OS/distro
   itself, the same unavoidable boundary as a syscall. What's still out of
   scope is third-party *convenience/abstraction* libraries built on top
   of that boundary for game-dev ergonomics — SDL, GLFW, Boost, the C++
   STL, etc. The line: "part of the base OS/distro" is fine, "a library
   that wraps a core system library for convenience" is not.
2. **libc is allowed, but the foundation layer still hand-rolls its own
   allocators, containers, string handling, and math** — for control,
   performance, and as a learning/portability exercise, not because
   linking libc is forbidden. Where calling libc directly is simpler and
   doesn't duplicate something the foundation layer already owns (e.g.
   wrapping a raw syscall, calling an OS API), that's fine. STL,
   exceptions, and RTTI remain unused regardless — the C++ language
   runtime overhead they bring isn't worth it, independent of the libc
   question. (The Phase 0 build's `-nostdlib -ffreestanding` flags are the
   foundation layer's own choice to prove itself dependency-free, not a
   whole-project ban — expect this to need revisiting once Phase 1 links
   `libwayland-client`, which itself depends on libc. Flagged as an open
   question in Phase 0's notes.)
3. **Linux window/input targets Wayland as the primary protocol, X11 as a
   fallback backend — both via their reference client libraries
   (`libwayland-client`, `libX11`/`libxcb`)**, not a hand-rolled
   wire-protocol client. Wayland is where the Linux desktop is heading and
   X11 is progressively being phased out by distributions, so Wayland gets
   built first; the X11 backend exists so the engine still runs on
   systems/WMs that haven't moved. Both backends conform to one internal
   window/input interface so upper layers never know which one is active.
   This resolves what was previously an open risk: Vulkan's WSI
   surface-creation extensions (`VK_KHR_wayland_surface`,
   `VK_KHR_xcb_surface`) take `libwayland-client`/`libxcb` object types
   directly, which is now simply "the type we already have," not a
   narrow driver-contract exception.
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
6. **Unreal asset parsing links Unreal Engine's own code (built as a DLL),
   not a from-scratch reverse-engineered parser.** Superseded: the earlier
   plan to reimplement the `.uasset`/`.umap` container format from
   community reverse-engineering docs (UAssetAPI/FModel/CUE4Parse) as a
   from-scratch parser. Instead, the offline Unreal-import tool (Phase
   8/10) links a DLL built from Unreal Engine's own source and drives its
   real `UObject`/package-loading system to load a project's content —
   correctness comes from the source, not RE guesswork, and it's far less
   work than reimplementing UE's serialization format by hand. This
   dependency is scoped to the offline import tool only, the same
   "dev-time, not runtime" category as CMake (assumption #8): the shipped
   engine/game binary never links or ships the Unreal DLL.
   **Open risk, not yet resolved:** Unreal Engine's source/binaries are
   governed by Epic's UE EULA, which is source-available under specific
   license terms, not permissively-licensed open source, and has its own
   rules about how UE code can be used and redistributed. Given this
   project's stated mission is to build a competing engine, the EULA
   terms need an actual review before Phase 8 work starts — don't assume
   this is fine by default.
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
11. **Every target platform — desktop, mobile, and console — is considered
    in interface design from day one, even though only Linux is
    implemented before Phase 11.** Implementation still follows the
    roadmap order (Linux → Windows/macOS → Android/iOS → consoles), but
    that's a build-order decision, not a design-scope one: the
    platform-layer and engine-facing interfaces (window, input, threads,
    files, time — see architecture layers below) must be shaped so that
    *no* later platform backend (Phases 11-14) ever forces a redesign of
    already-built engine layers, mobile/console included. Concretely,
    this means anticipating things Linux/desktop alone wouldn't surface
    even before those backends are implemented: touch/gamepad input
    alongside keyboard/mouse in the input model, app-lifecycle
    suspend/resume/low-memory events alongside open/resize/close in the
    window model, sandboxed/permissioned file I/O, and not assuming a
    keyboard/mouse or a traditional filesystem are always present.
    Interface shapes also shouldn't bake in Linux/Wayland-only concepts
    (e.g. compositor-specific semantics) where any other platform
    obviously needs to fit the same seam. When a later phase's concrete
    constraints (a specific console SDK, an NDA'd requirement) genuinely
    couldn't have been anticipated, that's a real exception to flag and
    fix explicitly — not something to design around casually.
12. **The resource system (Phase 7) has pluggable storage backends and
    versioned per-type binary formats.** Resources are loaded through a
    backend abstraction — pak-file (packed archive), filesystem (loose
    files, for fast dev iteration without repacking), and network
    (streamed from a remote source) — so upper layers never know which
    one served a given resource, same pattern as the RHI and the
    Wayland/X11 window backends. Every resource type (mesh, texture,
    material, level, ...) owns its own binary format with an explicit
    version header; the runtime loader supports old versions rather than
    breaking previously-cooked content when a format changes — this is
    the resolution to the versioning/migration question Phase 7 already
    flagged as open. Binary is the default and required format
    engine-wide; a parallel text format is optional per type, added only
    where it earns its keep (chiefly where hand-editing or VCS-friendly
    merging matters, e.g. levels/scenes) — the offline cooker converts
    between the two, and the binary form stays what ships.
13. **Gameplay scripting embeds real, well-known languages instead of a
    custom bytecode VM — starting with Lua via LuaJIT.** Deliberate
    exception to the "no third-party convenience dependencies" line
    (assumption #1): reimplementing a competitive language runtime plus
    JIT compiler from scratch is a categorically different, highly
    specialized undertaking from the OS-facing plumbing (windowing, RHI)
    this project otherwise reimplements — it's decades of compiler
    engineering with no engine-differentiating value in redoing it,
    unlike SDL/GLFW (thin convenience wrappers over things we're already
    building ourselves, where reimplementing *is* the point). LuaJIT
    ships inside the runtime engine/game binary — unlike CMake or the
    Unreal DLL, which are dev-time/import-tool-only — but its license
    (MIT) imposes no meaningful restriction, unlike the Unreal EULA
    question in assumption #6. Phase 9's scripting subsystem is designed
    with a pluggable language-backend seam (matching the pattern already
    used for the RHI, resource, and window backends) so more "well-known
    languages" can be added later as additive backends, not a redesign.
    Phase 10's Blueprint importer targets this scripting subsystem
    (transpiling Blueprint graphs into Lua) instead of a custom VM.
    **Open note, not yet resolved:** LuaJIT's JIT (runtime code
    generation) is disallowed on iOS App Store builds and typically on
    console certification; LuaJIT has an interpreter-only fallback mode
    for those. Phase 9's embedding should keep JIT-vs-interpreter a
    platform-config choice, not an API change (ties to assumption #11) —
    exact handling is deferred to Phases 12/14.

## Guiding principles

- **RHI-first rendering.** Gameplay/renderer code never calls Vulkan, DX12,
  or Metal directly — always through a common Render Hardware Interface.
- **Data-oriented ECS**, not an OOP scene graph.
- **Linux first, portable by design to every target platform.** Every
  system is built and proven on Linux before porting, but interfaces are
  shaped from day one so later ports — desktop, mobile, and console alike
  — are additive, never a redesign (see assumption #11).
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
Unreal Blueprint import        --> targets Scripting (Lua/LuaJIT)
Scripting (Lua/LuaJIT first, pluggable language backends)
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
  /asset        - resource system (pak/filesystem/network backends),
                  per-type versioned binary formats, offline tools
  /unreal_import - Unreal DLL embedding, mesh/texture/material/level
                   exporters, blueprint importer — builds as a separate
                   offline tool binary; the Unreal DLL dependency and
                   this module never link into the runtime engine library
  /scripting    - language-backend interface + lua/ (LuaJIT embedding,
                  first backend), engine API bindings
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
| 1 | Linux platform layer | Linux | [plans/phase-01-linux-platform.md](plans/phase-01-linux-platform.md) | Window opens via libwayland-client (X11 fallback backend also working), handles input/resize/close, stable frame timing |
| 2 | Vulkan RHI | Linux | [plans/phase-02-vulkan-rhi.md](plans/phase-02-vulkan-rhi.md) | RHI renders a lit, textured cube through Vulkan; no Vulkan calls leak outside `/engine/rhi/vulkan` |
| 3 | Renderer core | Linux | [plans/phase-03-renderer-core.md](plans/phase-03-renderer-core.md) | Render graph draws multiple objects, basic materials/lighting, depth buffering |
| 4 | ECS | Linux | [plans/phase-04-ecs.md](plans/phase-04-ecs.md) | Thousands of entities updated per frame, benchmarked vs. naive OOP baseline |
| 5 | Physics | Linux | [plans/phase-05-physics.md](plans/phase-05-physics.md) | Rigid body sim: gravity, collision, resolution for boxes/spheres/capsules |
| 6 | Audio | Linux | [plans/phase-06-audio.md](plans/phase-06-audio.md) | Custom mixer plays hand-decoded WAV, positional 3D audio |
| 7 | Asset pipeline & resource system | Linux | [plans/phase-07-asset-pipeline.md](plans/phase-07-asset-pipeline.md) | Offline cooker → versioned per-type binary formats, loaded at runtime through pak/filesystem resource backends |
| 8 | **Unreal asset & level import** | Linux | [plans/phase-08-unreal-asset-import.md](plans/phase-08-unreal-asset-import.md) | A real UE5 project's meshes/textures/materials/one level import and render in-engine |
| 9 | Scripting (Lua/LuaJIT) | Linux | [plans/phase-09-scripting.md](plans/phase-09-scripting.md) | LuaJIT runs a gameplay script that spawns/moves an entity via engine API bindings |
| 10 | **Unreal Blueprint import** | Linux | [plans/phase-10-unreal-blueprint-import.md](plans/phase-10-unreal-blueprint-import.md) | A simple real Blueprint's logic runs correctly, transpiled to Lua and executed via Phase 9's scripting subsystem |
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
