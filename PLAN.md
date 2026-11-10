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
- **Each phase has its own index file under `/plans`**
  (`plans/phase-NN-name.md`): Depends-on/Goal/Definition-of-done, links to
  that phase's day files in order, and Notes/open questions. It does not
  contain the day tasks inline.
- **Each day-sized task has its own file**, under
  `plans/phase-NN-name/day-NN-slug.md`, broken into specific,
  individually checkable sub-tasks — not one paragraph for the whole day.
  Near-term phases (00, 01, 02 so far) are the ones expected to have
  these; Phases 0-2 are all written this way. Later phases are scoped at
  the goal/deliverable level and get expanded into day files once the
  prior phase completes and reality has had a chance to correct the plan
  — but that expansion, and its commit to the repo, has to happen
  *before* the phase's work starts, not alongside it. No phase is
  startable while it's still at goal/deliverable level, or while its
  days are still undifferentiated paragraphs rather than files of
  specific sub-tasks.
- Every task has a **Done condition** — something concrete and checkable.
  If a task can't be checked, it isn't finished.
- Check off tasks (`[x]`) in the day files as they complete. Don't reorder
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
   allocators, containers, and string handling** — for control,
   performance, and as a learning/portability exercise, not because
   linking libc is forbidden. Where calling libc directly is simpler and
   doesn't duplicate something the foundation layer already owns (e.g.
   wrapping a raw syscall, calling an OS API), that's fine. On Windows the C library is the Universal C Runtime (UCRT), linked dynamically
   like libc on Linux (Phase 13; the phase file's earlier "no CRT" wording was wrong). **Amendment 2026-11-07 (Phase 10):** the C++ standard library's
   containers, strings, smart pointers, `std::function`, algorithms and streams are not used in `engine/`, `game/`, `tools/` or tests; the
   engine's `String`, `Vector`, `HashMap`, `UniquePtr`, `Span`, `Sort` and the `byte`/`u8`..`u128` vocabulary replace them, and the
   `std_container_check` gate (replaced by `std_usage_check` in Phase 16, assumption #23, which extends the rule to the whole standard and C library) enforces it (exceptions: the nlohmann/json boundary of `tools/shader_mint`, the aliases in `types.h`,
   Catch2's printers, and the older test directories, allow-listed with counts). **Amendment 2026-11-03:** `memcpy`/`memset`/`memmove`/`strlen`
   are no longer hand-rolled — the Phase 0 Day 3 `mem/mem.cpp` overrides of
   libc's own symbols were removed in favor of `<cstring>`, since they
   silently replaced libc's optimized versions for every linked library
   (libwayland/libX11/libxkbcommon) and conflicted with standard headers.
   `-fno-builtin` was dropped with them. **Scalar math
   functions** (`sqrt`/`sin`/`cos`/`acos`/`atan2`/etc.) call `<cmath>`
   directly rather than being hand-rolled — these are well-understood,
   already-optimal solved problems where a from-scratch version buys none
   of the control/performance/learning upside that justifies hand-rolling
   allocators or containers, and a subtly-wrong hand-rolled version is a
   correctness risk with no offsetting benefit. Vector/matrix/quaternion
   *types* (`Vec3`, `Mat4`, `Quaternion`, etc.) are still hand-rolled —
   it's their scalar building blocks that come from `<cmath>`, not the
   types themselves. **Concurrency primitives are the same category as
   scalar math, used directly rather than hand-rolled or wrapped in a
   custom engine type:** `std::thread`, `std::mutex`,
   `std::condition_variable`, `std::atomic`, and C++20 coroutines
   (`std::coroutine_handle`, `co_await`/`co_yield`) come straight from
   the standard library. Unlike Vulkan/DX12/Metal or Wayland/X11 —
   genuinely different platform-native APIs an RHI/window seam has to
   paper over — `std::thread`'s family is already the portable
   abstraction across every target platform this engine ships to, so a
   custom wrapper layer would just be re-solving a problem the standard
   library already solves correctly, the same trap as hand-rolled trig.
   `std::thread`'s constructor can throw `std::system_error` on failed
   thread creation; under this project's `-fno-exceptions`, that
   surfaces as `std::terminate` (an abort) rather than a catchable
   error — acceptable, since failed thread creation is an unrecoverable
   resource-exhaustion condition regardless of who calls into the OS
   for it. STL containers/algorithms remain unused regardless — this
   carve-out is for `<thread>`/`<mutex>`/`<atomic>`/`<coroutine>`
   specifically, not blanket STL approval. Exceptions and RTTI remain
   unused/disabled project-wide, independent of the libc question. **Resolved:** Phase 0 links libc normally from Day 1 — no
   `-nostdlib`/`-ffreestanding` — and its OS-interaction layer (Day 2)
   wraps libc functions directly (`write`, `read`, `open`, `close`,
   `mmap`, `munmap`, `mprotect`, `exit`) behind engine-named functions,
   rather than hand-rolling raw syscalls via inline assembly. This was
   previously left open pending Phase 1's `libwayland-client` linkage
   forcing the question anyway; no reason to defer it that long once the
   policy above already settled it. The wrapper layer still earns its
   keep as the seam later OS ports (Windows/macOS) implement against with
   their own native calls, same pattern as every other backend
   abstraction in this plan (RHI, window, resource, scripting).
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
   **Phase 15 Day 186 adds `libXcursor` and `libXi`** to the X11 backend: both are X.org client libraries of `libX11`'s own family, installed with
   it on every X11 system, used for the cursor theme and ARGB cursors (`libXcursor`) and for XInput 2 raw motion while the pointer is locked
   (`libXi`); the Wayland side uses only protocol bindings generated from `wayland-protocols` (cursor shape, pointer constraints, relative
   pointer), committed like xdg-shell, and does not use `libwayland-cursor`.
   **Day 187 adds `libXfixes`** (same family; selection-owner notifications, so a clipboard change by another program is seen) and the Wayland
   `primary-selection-unstable-v1` bindings, generated and committed the same way; the clipboard itself uses `wl_data_device` and the X11 selections.
4. **Images/assets start custom; shaders use an existing offline
   compiler (see #18).** A general PNG/JPEG importer is its own large
   project, so early phases use custom binary asset formats from our own
   offline tools. Writing our own GLSL/HLSL compiler is likewise out of
   scope for now, but that does not mean hand-writing SPIR-V: shaders are
   authored in a high-level language and compiled offline by an existing
   dev-time tool (assumption #18). GPU-native compressed texture formats (BCn/ASTC) are
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
   from-scratch parser. Instead, the offline Unreal-import tool (Phases
   24-25) links a DLL built from Unreal Engine's own source and drives its
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
   terms need an actual review before Phase 26 work starts — don't assume
   this is fine by default.
7. **First target CPU/ABI: x86-64 Linux, System V ABI.** Since Phase 13 also **Windows x64** (the Microsoft x64 calling convention,
   MSVC and `clang-cl` with the MSVC ABI). ARM64 is handled when a platform phase needs it (no Windows ARM64 build yet). **First target Unreal version: UE 5.3+** (property serialization
   has been relatively stable since unversioned properties landed in 4.25).
8. **Build tooling is a dev-time dependency, not a runtime one — CMake now,
   custom later.** "Zero third-party dependencies" governs what ships
   inside the engine/game binaries, not the tool that invokes the compiler
   to produce them (same boundary category as the compiler itself). Phase
   0 starts on CMake so early work isn't blocked reinventing a build
   system. Once the project's build needs (custom toolchain flags,
   cross-compiling to the later platform targets, asset minting steps) are
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
    implemented before Phase 13.** Implementation still follows the
    roadmap order (Linux → Windows/macOS → Android/iOS → consoles), but
    that's a build-order decision, not a design-scope one: the
    platform-layer and engine-facing interfaces (window, input, threads,
    files, time — see architecture layers below; windows and input are `engine/platform/`) must be shaped so that
    *no* later platform backend (Phases 13, 23, 24 and 25) ever forces a redesign of
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
    **Order (changed 2026-11-05):** the Windows and macOS ports (Phases
    12 and 13) now come right after the renderer core, so the RHI and the
    platform seams are tested against a second and third backend while
    they are still small. **Open, not decided:** how Phases 14-22 (text,
    UI, ECS, physics, audio, assets, scripting) keep the Windows and
    macOS builds working — a closing day in each phase that ports its
    platform-dependent parts, or a periodic catch-up phase. Decide when
    Phase 23 completes; record the answer here.
12. **The resource system (Phase 21) has pluggable storage backends and
    versioned per-type binary formats.** Resources are loaded through a
    backend abstraction — pak-file (packed archive), filesystem (loose
    files, for fast dev iteration without repacking), and network
    (streamed from a remote source) — so upper layers never know which
    one served a given resource, same pattern as the RHI and the
    Wayland/X11 window backends. Every resource type (mesh, texture,
    material, level, ...) owns its own binary format with an explicit
    version header; the runtime loader supports old versions rather than
    breaking previously-minted content when a format changes — this is
    the resolution to the versioning/migration question Phase 21 already
    flagged as open. Binary is the default and required format
    engine-wide; a parallel text format is optional per type, added only
    where it earns its keep (chiefly where hand-editing or VCS-friendly
    merging matters, e.g. levels/scenes) — the offline mint converts
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
    question in assumption #6. Phase 22's scripting subsystem is designed
    with a pluggable language-backend seam (matching the pattern already
    used for the RHI, resource, and window backends) so more "well-known
    languages" can be added later as additive backends, not a redesign.
    Phase 27's Blueprint importer targets this scripting subsystem
    (transpiling Blueprint graphs into Lua) instead of a custom VM.
    **Open note, not yet resolved:** LuaJIT's JIT (runtime code
    generation) is disallowed on iOS App Store builds and typically on
    console certification; LuaJIT has an interpreter-only fallback mode
    for those. Phase 22's embedding should keep JIT-vs-interpreter a
    platform-config choice, not an API change (ties to assumption #11) —
    exact handling is deferred to Phases 23/24.
14. **Unreal import (Phases 26-27) is sequenced after multiplatform
    support (Phases 13, 23, 24 and 25), not immediately after the asset pipeline
    (Phase 20).** Originally Phases 13 and 14 in an earlier draft's
    numbering (superseded by this reordering itself, not part of the
    current roadmap); moved so the engine reaches
    Linux+Windows+macOS/iOS+Android+console viability first, with Unreal
    import — the largest, most self-contained body of work in the plan —
    layered on afterward rather than gating or getting entangled with
    porting work. Assumption #11's "no platform ever forces a redesign"
    guarantee already meant this reordering needed no interface changes
    to the phases that existed then; it's a pure roadmap-order change.
    (The Windows and macOS ports were later pulled forward to Phases 13 and 14, and on 2026-11-08 the macOS port was postponed to Phase 23,
    right after the renderer core — see the 2026-11-05 progress-log
    entry; Android, iOS and consoles stay after Scripting, with Unreal
    import after them.)
15. **The job system (Phase 2) is its own layer above Platform, and jobs
    are C++20 coroutines — not stackful fibers.** The engine is heavily
    multithreaded by design, so every later multithreaded subsystem (ECS
    parallel system scheduling, renderer command recording, physics,
    audio, asset streaming) needs a shared, portable way to fan out and
    join work rather than each hand-rolling its own thread pool. Phase 1
    Day 15 owns worker-thread creation using `std::thread`/`std::mutex`/
    `std::condition_variable` directly (assumption #2 — these are
    already portable, not OS-specific, so there's no per-platform piece
    to isolate here the way Vulkan/Wayland need one); the work-stealing
    scheduler built on top of those primitives is a portable algorithm,
    so it gets its own layer between Platform and RHI — the same
    relationship RHI already has to Platform's raw Vulkan driver-loading
    (Day 17).
    Stackful fibers were considered for job suspension (letting a worker
    thread pick up other work while a job waits on a dependency) but
    rejected: fiber context-switching requires manipulating raw
    register/stack state at some level, which is either hand-written
    per-ABI assembly (contrary to this phase's whole point — being
    portable) or a wrapped OS-native facility (`ucontext` on POSIX, Win32
    Fibers on Windows, with macOS/iOS support unresolved) — a new
    per-platform dependency to track for every future port. Instead, jobs
    are authored as C++20 coroutines: waiting on a dependency is
    `co_await` on a `Counter` awaitable, which suspends the coroutine and
    returns control to the scheduler without blocking the OS thread. This
    is pure standard C++ — nothing OS- or ABI-specific anywhere in this
    phase — and the same mechanism doubles as the async-I/O primitive
    (Phase 21's asset streaming), rather than needing a second suspension
    mechanism alongside fibers. Coroutine-frame allocation is routed
    through a Foundation pool allocator (via `promise_type::operator
    new/delete`) rather than the global default `new`, keeping the
    no-malloc-by-default rule intact.
16. **Test tooling is a dev-time dependency, not a runtime one — Catch2
    v3 for the test harness.** Superseded: Phase 0's test harness day
    (Day 15 as of the Unicode string support insertion; Day 12 when this
    assumption was first written) originally planned a hand-rolled,
    framework-free test runner, on the theory
    that "zero third-party dependencies" applied to test tooling the
    same as everything else. Reconsidered: test binaries are dev/CI
    artifacts that never link into the shipped engine/game binary — the
    same "dev-time, not runtime" category as CMake (assumption #8), and
    an even cleaner case than Lua/LuaJIT (assumption #13, which does
    ship at runtime). Reimplementing a test framework's discovery/
    assertion/reporting machinery has no engine-differentiating value,
    unlike the OS-facing systems (allocators, containers, RHI) this
    project exists to hand-roll. Catch2 is vendored into the repo under
    `third_party/catch2` (a pinned release's source tree, not a
    submodule) rather than fetched via CMake `FetchContent`, for fully
    offline/reproducible builds — the tradeoff being repo size and
    manual updates instead of a floating/network dependency at
    configure time. v3 (compiled library, actively developed) was
    chosen over v2 (single-header, maintenance-mode) despite v3 being
    the more involved vendoring story.
    **Technical note:** Catch2's `REQUIRE`/`CHECK` macros are
    exception-based; since the engine/game runtime libraries build with
    `-fno-exceptions` project-wide (Day 1), exceptions are enabled only
    for the test executable target(s), not the `foundation` (or any
    later engine) library target itself — production code under test
    still compiles and runs exception-free. Whether `-fno-rtti` also
    needs a test-target carve-out is unverified until Day 15 actually
    builds against Catch2 — flagged there, not assumed here.
17. **Font rendering (Phase 14) is backed by FreeType — a deliberate,
    individually-justified third-party dependency exception, the same
    category as Lua/LuaJIT (assumption #13), not Catch2 (assumption
    #16, dev-time only).** FreeType ships inside the runtime engine/
    game binary: it parses font files (TrueType/OpenType/Type1/etc, a
    genuinely complex format family with decades of hinting-algorithm
    and rendering-edge-case history behind it) and rasterizes glyph
    outlines to bitmaps. Reimplementing a font rasterizer from scratch
    has the same "solved problem, no engine-differentiating value"
    profile the Lua and Catch2 decisions were made on — unlike the
    OS-facing systems (allocators, containers, RHI, the job scheduler)
    this project exists to hand-roll.
    Vendored as source under `third_party/freetype/` (a pinned release,
    built from source alongside the engine — same pattern as Catch2,
    assumption #16), not linked against a distro-provided
    `libfreetype.so`: unlike `libwayland-client`/`libX11` (assumption
    #3, the reference client libraries *for the OS's own display
    protocol*, guaranteed present on every Linux desktop this engine
    targets), FreeType is a portable third-party library that happens
    to often be preinstalled on desktop Linux, but isn't guaranteed
    present the same way on every platform in this project's roadmap
    (Android, iOS, consoles — Phases 24-25) — the same reasoning that
    put LuaJIT under vendored/embedded rather than system-linked.
    **Pinned (Phase 14 Day 175): FreeType 2.15.0** (`freetype-2.15.0.tar.xz`, SHA-256 `93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5`),
    used under the **FreeType License (FTL, BSD-style with a credit clause; the license text is `third_party/freetype/docs/FTL.TXT`; the credit line goes into the notices file a first release adds, filed in `KNOWN-ISSUES.md`)**, not the GPLv2
    alternative. The engine builds TrueType and OpenType/CFF outlines, the smooth rasterizer and the auto-hinter only (no Type 1, PCF, BDF, SVG, WOFF/zlib,
    PNG, HarfBuzz); every block FreeType allocates comes from the engine's heap through a counting `FT_Memory`; and one file, `engine/text/font_freetype.cpp`,
    names FreeType (the `platform_neutral_check` library boundary). The font the engine ships is Liberation Sans Regular (SIL OFL 1.1, `assets/fonts/`).
    **Scope for Phase 14:** font parsing, glyph rasterization/hinting,
    and glyph-atlas packing (uploaded through the Phase 4 RHI as a
    texture) are FreeType's job. Text *shaping* for complex scripts
    (Arabic/Indic joining and reordering, contextual ligatures) is
    explicitly out of scope for Phase 14 — that's HarfBuzz's problem
    domain, a separate library this plan does not commit to yet.
    Phase 14 targets Latin-script-family text correctly (the common
    case for this project's near-term game/editor UI needs) using
    FreeType's per-glyph advance-width metrics directly; cursor
    placement, line-break candidates, and multi-codepoint clusters
    (combining marks, emoji ZWJ sequences) reuse the foundation
    string module's existing grapheme-clustering support (Days 10-12)
    rather than duplicating that logic. Revisit HarfBuzz if/when a
    real complex-script requirement shows up (flagged, not assumed
    needed).

18. **Shader toolchain: Slang via `slangc` (GLSL retired on Phase 5 Day 70).** Shaders are never compiled at runtime in a shipped
    build: the engine loads precompiled bytecode (SPIR-V, and DXIL since Phase 13; MSL in Phase 23), so no shader compiler is linked
    into the engine/game binary. **Phase 13:** `slangc` mints DXIL from the same sources (with `dxc`'s `dxcompiler.dll` and `dxil.dll` from
    the Windows SDK), committed as `*.dxil.inc` beside `*.spv.inc`; shaders carry one blob per format (`rhi::ShaderBlobSet`) and a device
    takes the one its `DeviceCaps::shader_format` names; the D3D12 build of each shader negates clip-space Y (`ENGINE_CLIP`), and D3D12
    reads the object index from an instance-rate stream (no base instance).
    The compiler is a dev-time tool in the same boundary category as
    CMake and the C++ compiler (assumption #8) and Catch2 (#16).
    - **Phase 4 (historical, retired):** shaders were GLSL 4.50 sources
      (`*.vert`, `*.frag`, `*.comp`) under `shaders/`, compiled by
      `glslc` (distro `shaderc`) and checked with `spirv-val`,
      then embedded by our own `spv_embed` tool (folded into `shader_mint` on Phase 5 Day 71) into committed
      `*.spv.inc` word arrays. The tools are optional: a checkout without
      them builds from the committed arrays, and a ctest recompiles and
      compares only when they are present. Hand-written SPIR-V assembly
      was never used.
    - **Settled 2026-11-04 (for Phase 5 on): Slang.** The long-term source
      language is Slang, compiled offline by `slangc` to SPIR-V now and to
      DXIL/HLSL and MSL when Phases 13 and 23 need them, with Slang's reflection
      and specialization feeding the material system. Chosen for
      platform portability: one source for Vulkan, D3D12/Xbox, Metal and
      Android, with HLSL/GLSL output for console vendor compilers, built-in
      reflection and permutations. Rejected: GLSL + SPIRV-Cross (weakest
      source language on every non-Vulkan target, lossy cross-compilation),
      HLSL + dxc (runner-up; safest on consoles but no built-in
      permutations and MSL still goes through SPIRV-Cross), a custom
      front-end (a compiler is its own project). Slang stays an offline dev
      tool in the same boundary category as CMake and `glslc` were (#8,
      #16): nothing is linked into the runtime, and compiled outputs
      (`*.spv.inc` plus reflection) stay committed so a checkout without
      `slangc` still builds. **Done:** the Phase 4 GLSL shaders were ported on
      Phase 5 Days 69-70 (`slangc` v2026.18.2, flags and pitfalls in
      `shaders/README.md`); the Slang output reproduced every golden image
      unchanged, and the GLSL sources and the `glslc` path were removed on
      Day 70. Open items for Phases 13 and 23: Metal renames an entry point
      called `main`, and no Slang flag flips clip-space Y for HLSL/Metal
      (see `shaders/README.md`, "Other targets").
    - The RHI already takes bytecode (`ShaderDesc` holds SPIR-V words); the
      Day 67 portability review (section 3) found that a per-backend blob
      would add a format tag to `ShaderDesc`, and that any translator must
      flip clip-space Y for D3D12/Metal. Neither blocks Phase 5.

19. **Offline tools may use a vetted third-party library where the job is
    solved and has no engine-differentiating value - starting with
    nlohmann/json in `tools/shader_mint` (decided 2026-11-05).** The Day 71
    shader mint tool must read `slangc -reflection-json` output. Writing a
    JSON parser is not what this project exists to hand-roll, so the tool
    uses nlohmann/json v3.12.0 (MIT, single header), vendored as source under
    `third_party/nlohmann_json/` (same pattern as Catch2, #16: pinned
    release, no submodule, no `FetchContent`). Scope, which is what makes
    this an exception and not a loophole: the library is linked into
    **offline tools only** (today `shader_mint`), never into the engine, a
    game, the renderer or any engine test target (the tool's own tests link it), and the tool's output (the
    committed `*.spv.inc` / `*.refl.inc`) is what the engine consumes, so
    a checkout still builds without `slangc` and the engine binary
    contains no JSON code. The tool is built exception-free like the rest
    (`-fno-exceptions`; the library then aborts only on programmer errors,
    which the tool avoids by checking types and using `value()`/`contains()`
    accessors and `parse(..., allow_exceptions=false)`). Because that library
    is built on `std::string` and the standard containers, the offline tool
    (and only the tool and its tests) also uses `std::string`, `std::vector`
    and `<algorithm>`; the rule against the STL applies to the engine's
    runtime libraries, which stay on the foundation containers. Any further
    third-party library in a tool or in the engine still needs its own
    justified entry here.

20. **Debugging facilities (Phase 7) are built on existing libraries, not hand-rolled — a deliberate, individually
    justified exception to the zero-third-party-dependencies rule (decided 2026-11-06, libraries chosen by spike on
    Phase 7 Day 91; see `docs/superpowers/specs/2026-11-06-debugging-design.md`).** Crash handling is subtle
    (async-signal safety, unwinding through code without frame pointers, dumping every thread, surviving a corrupted
    heap) and the engine does not differentiate on it, so it uses two vetted libraries, each vendored as source under
    `third_party/` with a pinned version, a `VENDORED.md` and its license (same pattern as Catch2, #16: no submodule, no
    `FetchContent`, builds offline): **libbacktrace** (BSD-3-Clause, commit `0b9b49cf4a`; stack unwinding, symbol names and
    file:line, ELF/Mach-O/PE with DWARF; documented async-signal-safe, measured at zero allocations in a handler) and
    **sentry-native 0.17.6** (MIT; CMake, C API) used only for crash capture and minidumps, with its **Breakpad** backend
    and the `none` transport (Breakpad: BSD-3-Clause). The Crashpad backend was rejected by the spike: on Linux it
    requires libcurl at build time and its handler executable needs libcurl and about 25 shared libraries at run time.
    Scope, which is what makes this an exception and not a loophole: both libraries link into the runtime programs that
    install the crash handler (the demos, the benches, games) and the Phase 7 tests, behind a facade in
    `engine/foundation/debug/`: only the two implementation files include the libraries' headers (`backtrace.h`,
    `sentry.h`; a gate enforces it, like the RHI boundary), both libraries have C interfaces so no STL type reaches
    engine code, and they are built with their own flags like Catch2. **Nothing is ever uploaded:** the transport is
    `none` (the facade gives the library a transport that only copies envelopes into a local `saved/` folder, because the null
    transport would delete a crash's dump on the next start), our options have no URL or DSN field, dumps stay on the
    developer's disk, and a test fails if a binary links libcurl, a TLS library or winhttp. What stays hand-written because it is engine-specific or small: the
    assert/ensure/verify family, the crash context and recent-log ring buffer, the death-test helper (Catch2 has none), the
    core-dump setup and scripts, and a small dev tool that extracts the minidump from sentry's envelope. Windows and macOS
    (Phases 13 and 23) reuse sentry-native with Breakpad. **Phase 13 decided the symbol gap:** libbacktrace reads DWARF only, so on
    Windows the facade resolves names and lines with **DbgHelp** from the PDBs (the operating system's own symbol library, not a third-party
    one; Day 154), and libbacktrace is not built there; the minidump scripts are PowerShell and read only a local symbol store. Any further third-party library
    still needs its own justified entry here.

21. **All file and directory access goes through one layer (Phase 9): `io::Path`, `io::File`, `io::Directory`.** `File` is an interface with
    synchronous and asynchronous read and write, so a file can later be backed by the network (the resource system's storage backends,
    assumption #12, are `File` implementations); only local files are implemented in Phase 9. `Path` is a value type with no syscalls and a
    `PathStyle`, so Windows path rules are tested on Linux. Errors are values (`io::Error`), not exceptions or `errno`. The foundation defines
    completion-callback async operations and an `IoExecutor` seam; the job system (above the foundation) supplies the executor and the `co_await`
    adapters. Documented exceptions, enforced by the `file_access_check` gate (an allow list with a reason per entry; a stale entry fails): the signal-safe
    crash-report writer and console output keep raw descriptors, the backend itself, `memfd` shared memory, tests that plant raw file-system shapes on
    purpose, and third-party code. Built in Phase 9 (Days 115-124); a new platform implements `engine/foundation/io/BACKEND.md`. The settings system
    (Phase 11) is built on this layer.

22. **Anything a user can configure is a setting in one registry (Phase 11).** `settings::Registry` (foundation) holds every setting's type, default, range, enum names, apply
    class, label, group, environment variable and flag; values are read through `Setting<T>` handles (relaxed atomic reads) or `Settings` snapshots. Where values live is a
    `ConfigStore` interface (file store with a comment-preserving text format and atomic saves is the only one built; the Windows registry, `NSUserDefaults`, `SharedPreferences` and
    save-data stores follow the checklist `engine/foundation/settings/BACKEND.md`), combined by a `LayerStack` (system, user, project, custom, environment, command line) with
    provenance; changes go through `Settings::Apply` with rollback and notify listeners; files carry schema versions and migrations. The renderer's settings are clients of it. The
    `config_access_check` gate keeps new `getenv` and `--flag` parsing out of `engine/` and `game/` except where an allow-list entry gives a reason (bootstrap variables read before
    any `Settings` exists, the death-test protocol, launch-only options of demos and benchmarks). Built in Phase 11 (Days 130-139).

23. **The C++ and C standard library are used only through wrapper modules, one per category (Phase 16).** Amends assumption #2, which let concurrency primitives, `<cmath>`, `<chrono>`, `<type_traits>`, `<utility>`
    and the C library through `std::` be used directly. From Phase 16, `engine/`, `game/`, `tools/` and `tests/` include no standard header and spell no `std::` name (or unqualified C-library name); the facilities are wrapped by
    inline zero-cost engine types in the module of their category: `lang` (traits, `Move`, `Limits`, `Ordering`, scalar math), `sync` (`Atomic<T>`, `Mutex`, `LockGuard`, `ConditionVariable`, `Thread`), `cstr` (raw memory, C strings, formatting,
    number parsing; layer 0 like `lang` and `sync`, because `types.h` needs them), `os` (console output, `abort`/`exit`, environment, error number, signals, clocks), `jobs` (`CoroutineHandle`), and the Phase 10 containers. Several modules include the standard library, each its own category; the gate `std_usage_check` lists the
    headers each may include. Names the compiler requires (`std::coroutine_traits`, `std::initializer_list`, `std::strong_ordering`, `std::align_val_t`) stay in the wrapper modules; `third_party/`, the Catch2 printers and the nlohmann/json boundary
    of `shader_mint` are the other exceptions. **Standing policy (the user, 2026-11-09): code always uses the wrapped types, never `std`, from now on** - a new module that needs a facility with no wrapper adds the wrapper to the module of its category first, and `std_usage_check` (allow list `path|headers|reason`, a stale entry fails) keeps it so. Decided with the user on 2026-11-09 (everything, tests included; wrappers by category). Design: `docs/superpowers/specs/2026-11-09-std-wrappers-design.md`; plan: `plans/phase-16-std-wrappers.md`.

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
- **Test by phase; close a phase with the full suite.** The suite is large (1800+ tests in three sanitizer flavors) and
  most of it cannot be affected by today's work, so day-to-day runs cover the current phase plus the modules a change
  touched (`scripts/test-phase.sh`), and every phase ends with the whole suite in every flavor. Every test belongs to
  exactly one phase (`tests/phases.txt`), and a reduced run always says how much it left out.
- **Minting, not cooking, and minting is deterministic.** Our offline asset
  and shader processing is called *minting* (tools are "mints" or
  "minters", outputs are "minted"); "cook" is used only to describe
  Unreal's own pipeline (uncooked projects, cooked games). The name is the
  requirement: a mint stamps out identical coins, so the same inputs and
  the same tool version must produce byte-identical outputs, with no
  timestamps, absolute paths, hash-map iteration order or thread-timing
  dependence in the result. `shaders_check` already enforces this for
  shaders by recompiling and comparing bytes; every Phase 21 minter gets
  an equivalent determinism test.
 A day that adds or changes public API
  (headers named by `docs/reference/modules.txt`) ships its `///` comments,
  the regenerated `docs/reference/`, and the updated guide page in the same
  commit; `docgen lint`, link, sample and staleness checks (`ctest -L docs`)
  enforce it. Rule text: `CLAUDE.md`; tooling: Phase 3.
- **Owning arrays are foundation containers, never raw `new[]`/`delete[]`.**
  Hand-paired `new T[n]` / `delete[] p` is correct but error-prone (leaks
  on early-return paths, mismatched delete forms). The foundation has two
  array types and nothing else is needed: `Array<T, N>` is `std::array`-like
  (compile-time size, inline storage, so an object holding one is its
  members plus the array), and `Vector<T>` is every other case (runtime
  size or growth; it owns heap storage by default and frees it). `Vector`
  is what Phase 0 Day 6 called `Array`, renamed in Phase 5 Day 80a. Raw
  array `new` is allowed only inside the foundation allocators themselves.
  Enforced by a `ctest` source scan (Day 80a); applies to every later phase.
  Single objects follow the same rule (decided 2026-11-05, Phase 5 Day 80b):
  `UniquePtr<T, Deleter>` owns them, and raw `new`/`delete` is banned in
  `engine/` and `game/` outside the foundation allocators (placement `new`
  into container-owned storage is the only other form).
- **Quiet by default.** Logging is channel-based with per-channel verbosity
  and per-user configuration from day one — no Unreal-style log spam.

## Architecture layers (dependency flows downward only)

The long-term diagram. The modules that exist, their layers, allowed dependencies and the measured graph (including the cycle inside the
foundation, found 2026-11-07) are in `ARCHITECTURE.md`, which is the source of truth for dependencies.

```
Game / Demo content
Editor tooling                 --> built on the UI system (and Viewport widget)
Unreal Blueprint import        --> targets Scripting (Lua/LuaJIT)
Scripting (Lua/LuaJIT first, pluggable language backends)
Unreal asset & level import    --> targets Asset pipeline / ECS / Renderer
Physics | Audio | Asset Pipeline
ECS
UI system (retained widgets, layout, input routing, Viewport widget
          hosting a rendered 3D world) --> consumes Text + Renderer render targets
Text (FreeType-backed glyph rasterization/atlasing, built on Renderer/RHI)
Renderer core
RHI  (Vulkan | DX12 | Metal)
Job System (work-stealing scheduler, jobs-as-coroutines)
Platform layer (window, input, threads, files, time)
Foundation (allocators, containers, string, math, assert)
libc / raw syscalls / OS ABI
```

## Repository layout

```
/engine
  /foundation   - one library per layer (Phase 12): foundation_base (types,
                  os/ primitives and services, allocators, assert and log,
                  containers, string, math), foundation_io (Path, File,
                  Directory: the file layer, Phase 9), foundation_debug
                  (Phase 7), foundation_settings (Phase 11)
  /platform     - windows and input (window.h, input.h, window_host.h) and
                  their per-OS backends: linux/ (Wayland, X11), later
                  windows/, macos/, android/, ios/; the library `platform`,
                  the only one that links the display libraries
  /jobs         - work-stealing job scheduler, coroutine-based Job/Task
                  type and Counter/await primitives, async I/O helpers
  /rhi          - backend-agnostic interface + vulkan/, dx12/, metal/
  /renderer     - render graph, materials, passes (built on rhi)
  /text         - FreeType-backed font loading/glyph rasterization,
                  glyph atlas packing, text layout (line breaking,
                  cursor/selection positions) built on the foundation
                  string module's grapheme clustering (Days 10-12)
  /ui           - retained widget toolkit built on rhi/renderer/text:
                  layout, input routing, run-time-swappable theming,
                  menus/buttons/comboboxes/scrollbars/labels/text
                  fields, the Viewport widget that hosts a rendered 3D
                  world, and UI surfaces that render a widget tree into
                  a texture (in-world screens)
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
/tools           - offline asset minters, custom build tooling
/tests           - per-layer test harnesses using Catch2 v3 (vendored,
                  see assumption #16), discovered via CTest
/third_party     - vendored third-party source (Catch2 v3 under catch2/)
/plans           - per-phase index files (this index links to each);
                  phase-NN-name/day-NN-slug.md holds each day's tasks
PLAN.md
```

## Phased roadmap

| # | Phase | Platforms | Plan file | Definition of done |
|---|-------|-----------|-----------|---------------------|
| 0 | Foundation layer | none (freestanding) | [plans/phase-00-foundation.md](plans/phase-00-foundation.md) | Custom allocators/containers/string/math pass the custom test harness |
| 1 | Linux platform layer | Linux | [plans/phase-01-linux-platform.md](plans/phase-01-linux-platform.md) | Window opens via libwayland-client (X11 fallback backend also working), handles input/resize/close, stable frame timing |
| 2 | Job System & Coroutines | none (portable) | [plans/phase-02-job-system.md](plans/phase-02-job-system.md) | Work-stealing scheduler runs thousands of dependency-chained jobs race-free under sanitizers; throughput scales with core count |
| 3 | **Documentation tooling (`docgen`)** | none (dev tool) | [plans/phase-03-docs-tooling.md](plans/phase-03-docs-tooling.md) | Own C++ tool generates the API reference from `///` comments, checks links, compiles doc samples and lints coverage; wired into ctest |
| 4 | Vulkan RHI | Linux | [plans/phase-04-vulkan-rhi.md](plans/phase-04-vulkan-rhi.md) | RHI renders a lit, textured cube through Vulkan; no Vulkan calls leak outside `/engine/rhi/vulkan` |
| 5 | Renderer core (complete) | Linux | [plans/phase-05-renderer-core.md](plans/phase-05-renderer-core.md) | Render graph draws multiple objects, basic materials/lighting, depth buffering |
| 6 | **Test scoping** (complete) | none (dev tooling) | [plans/phase-06-test-scoping.md](plans/phase-06-test-scoping.md) | Every test belongs to a phase; day-to-day work re-runs only the current phase's tests (plus the modules a change touches), and the full suite, in all sanitizer flavors, runs when a phase completes |
| 7 | **Debugging facilities** (complete) | Linux first (Windows/macOS designed in) | [plans/phase-07-debugging-facilities.md](plans/phase-07-debugging-facilities.md) | Stack traces with symbols, a crash handler that writes core dumps and minidumps, and assert/ensure/verify macros with a pluggable handler; a crashing test process yields a readable report |
| 8 | **RHI reset** (complete) | Linux | [plans/phase-08-rhi-reset.md](plans/phase-08-rhi-reset.md) | Any rendering setting (GPU, present mode, resolution, validation, quality, ...) changes at run time by re-initializing the RHI and recreating device resources, without restarting the app; device loss recovers through the same path |
| 9 | **File handling** (`Path`, `File`, `Directory`) | Linux first (Windows/macOS designed in) | [plans/phase-09-file-handling.md](plans/phase-09-file-handling.md) | One file and path layer for the whole engine: a `Path` value type, a `File` interface with synchronous and asynchronous read and write (only local files implemented; the interface fits network-backed files), a `Directory` class; every existing file-access site ported and a gate keeps new code from bypassing the layer |
| 10 | **Standard types and container hygiene** (complete 2026-11-07) | Linux first | [plans/phase-10-types-and-containers.md](plans/phase-10-types-and-containers.md) | `byte`, portable `i128`/`u128`, `Span`/`ByteSpan`; byte buffers and integer spellings standardized; `game/`, the Phase 9 test helpers and `tools/shader_mint` (outside its JSON boundary) moved onto the engine's containers; a `std_container_check` gate keeps `std::` containers out of `engine/`, `game/` and `tools/` |
| 11 | **Settings system** | Linux first | [plans/phase-11-settings-system.md](plans/phase-11-settings-system.md) | One engine-wide settings registry, config-file classes (text format with comments, atomic saves), layered sources and precedence, change notification, versioning; the rendering settings of Phase 8 migrate onto it; the demos get `--settings`, `--save-settings` and `--set` |
| 12 | **Module layering** (complete, Days 140-146) | none (shared code, run on Linux) | [plans/phase-12-module-layering.md](plans/phase-12-module-layering.md) | No dependency cycle: the foundation is one library per layer, windows and input are their own `platform` library under `engine/platform/`, no program includes a per-platform header, and a `module_deps_check` gate keeps it that way (`ARCHITECTURE.md`) |
| 13 | Windows port + DX12 backend (Days 147-174; complete) | Windows | [plans/phase-13-windows-dx12.md](plans/phase-13-windows-dx12.md) | Everything built so far running on Windows; the Phase 4 golden images and Phase 5 scene tests pass through DX12 and Vulkan with validation silent; DX12 passes the lifecycle suite; the backend is chosen at start (no run-time switch); Linux still passes |
| 14 | **Font & text rendering** | Linux | [plans/phase-14-font-text-rendering.md](plans/phase-14-font-text-rendering.md) | Arbitrary UTF-8 text (via FreeType-rasterized glyphs, Day 10-12 grapheme clustering for cursor/line-break correctness) renders on screen through the RHI at an interactive frame rate, and the same layout can be built as an extruded 3D mesh from the vector outlines (color emoji via `COLR` v0 and bitmap glyphs) |
| 15 | **UI system (widgets & viewport)** | Linux | [plans/phase-15-ui-system.md](plans/phase-15-ui-system.md) | Retained widget toolkit (menus, buttons, comboboxes, scrollbars, labels, text fields...) on the RHI and Phase 14 text, with run-time-swappable themes, a viewport widget that renders the 3D world inside a resizable panel and routes input correctly, and UI surfaces that render into a texture (in-world screens) |
| 16 | **Standard-library wrappers** | Linux, Windows | [plans/phase-16-std-wrappers.md](plans/phase-16-std-wrappers.md) | Every use of the C++ standard library (and the C library) sits inside one wrapper module per category; a gate fails any other file that includes a standard header |
| 17 | **3D text, large 2D text & colored glyphs** | Linux | [plans/phase-17-3d-text-and-color-glyphs.md](plans/phase-17-3d-text-and-color-glyphs.md) | Text above Phase 14's maximum raster size draws sharp as filled 2D outline meshes, and text laid out by Phase 14 becomes an extruded, optionally beveled 3D mesh from the vector glyph outlines (own flattening, triangulation with holes, extrusion; no third-party tessellator) that draws through the Phase 5 renderer; colored glyphs (`COLR` v0 layers, `CBDT`/`sbix` bitmaps) on the 2D and 3D paths. Split out of Phase 14 on 2026-11-08 |
| 18 | ECS | Linux | [plans/phase-18-ecs.md](plans/phase-18-ecs.md) | Thousands of entities updated per frame, benchmarked vs. naive OOP baseline |
| 19 | Physics | Linux | [plans/phase-19-physics.md](plans/phase-19-physics.md) | Rigid body sim: gravity, collision, resolution for boxes/spheres/capsules |
| 20 | Audio | Linux, Windows (macOS once Phase 23 lands; mobile in Phase 24) | [plans/phase-20-audio.md](plans/phase-20-audio.md) | Custom mixer plays hand-decoded WAV, positional 3D audio, on Linux and Windows through one device interface (macOS joins in Phase 23) |
| 21 | Asset pipeline & resource system | Linux | [plans/phase-21-asset-pipeline.md](plans/phase-21-asset-pipeline.md) | Offline mint → versioned per-type binary formats, loaded at runtime through pak/filesystem resource backends |
| 22 | Scripting (Lua/LuaJIT) | Linux | [plans/phase-22-scripting.md](plans/phase-22-scripting.md) | LuaJIT runs a gameplay script that spawns/moves an entity via engine API bindings |
| 23 | macOS port + Metal backend (postponed 2026-11-08, moved from 14) | macOS | [plans/phase-23-macos-metal.md](plans/phase-23-macos-metal.md) | Phases 1-5's functionality running on macOS through the Metal RHI backend; the same golden images and scene tests pass |
| 24 | Android + iOS ports | Android, iOS | [plans/phase-24-android-ios.md](plans/phase-24-android-ios.md) | Android reuses the Vulkan backend, iOS the Metal backend from Phase 23; touch input and app-lifecycle platform layers |
| 25 | Console ports | Xbox, PS5 | [plans/phase-25-consoles.md](plans/phase-25-consoles.md) | Gated on dev kit/NDA access; same RHI, new platform layer |
| 26 | **Unreal asset & level import** | Linux | [plans/phase-26-unreal-asset-import.md](plans/phase-26-unreal-asset-import.md) | A real UE5 project's meshes/textures/materials/one level import and render in-engine |
| 27 | **Unreal Blueprint import** | Linux | [plans/phase-27-unreal-blueprint-import.md](plans/phase-27-unreal-blueprint-import.md) | A simple real Blueprint's logic runs correctly, transpiled to Lua and executed via Phase 22's scripting subsystem |
| 28 | Editor tooling | Linux first | [plans/phase-28-editor.md](plans/phase-28-editor.md) | In-engine editor: scene view, entity inspector, asset browser |
| 29 | First playable demo | Linux, Windows | [plans/phase-29-first-playable-demo.md](plans/phase-29-first-playable-demo.md) | A small real game built with the engine, dogfooding every system |
| 30 | Feature-parity push | all | [plans/phase-30-feature-parity.md](plans/phase-30-feature-parity.md) | Ongoing: advanced rendering, world streaming, networking — scoped when reached |

## Progress log

_(Append dated entries here as phases/milestones complete, noting anything
that changed from the plan and why.)_

- **2026-11-03 — Phase 2 plan revised after verification.** Added Day 27
  (jobs library, TSan target, thread-safe frame pool); former Days 27-34
  are now 28-35. Settled worker model A (main helps while waiting),
  logical-CPU participant count with `ENGINE_JOB_THREADS` override, job
  ownership/completion rules, injection queue, counter protocol, and I/O
  thread design — see `plans/phase-02-job-system.md` "Design decisions".
  Also this day: removed the hand-rolled libc `mem*`/`strlen` overrides
  (assumption #2 amendment).
- **2026-11-03 — Phase 2 (Job System) implemented, Days 27-35.** Worker
  model A as planned (N-1 pinned workers + main helping in `HelpUntil`).
  One deviation from the day files, recorded in Day 32: `Counter` uses a
  single packed atomic word (waiter bit + count) instead of a separate
  slot exchanged on both sides — the original sketch let the awaiter
  destroy the Counter while a decrementer still touched it. Each day ended
  with a `/code-review high` whose findings are recorded in the day files.
  `FramePool` size classes were confirmed with real frame sizes (all fit
  <=240 B). Phase 4 needs its day files before it can start.
- **2026-11-03 — Phase 3 (documentation tooling) added.** Own docs tool
  in C++ on the foundation (no Doxygen/libclang), planned as Days 36-44
  between Phase 2 and Phase 4; skeleton end-user docs exist under `docs/`.

- **2026-11-03 — UI system added as a new Phase 7.** Found at the desk:
  no phase owned a GUI (only one bullet inside the last-but-one Editor
  phase). Inserted "UI system (widgets & viewport)" after Font & Text —
  retained widget toolkit (menus, buttons, comboboxes, scrollbars, labels,
  ...) plus a viewport widget hosting the rendered 3D world — and
  renumbered the old Phases 7-24 to 8-24 (files renamed, all cross-
  references updated; historical dated design docs under
  `docs/superpowers/specs/` keep their original numbers). The Editor is
  now Phase 24 and builds on Phase 7.

- **2026-11-04 — Phase 4 (Vulkan RHI) broken into Days 45-67.** 23 day
  files under `plans/phase-04-vulkan-rhi/`: design spec and build
  skeleton, core types, null backend, native-window seam, Vulkan
  loader/instance/device/allocator, buffers/textures/swapchain, frame loop,
  SPIR-V workflow, pipelines, bind groups, triangle/compute/cube/lit-cube
  milestones, boundary gate, portability review. Settled calls: Vulkan 1.3
  loaded at runtime, explicit resource-state barriers, dynamic rendering,
  GLSL shaders compiled offline by glslc (assumption #18), GPU tests
  skip without a device.

- **2026-11-04 — Shader toolchain decision.** Hand-written SPIR-V assembly
  dropped: Phase 4 shaders are GLSL compiled offline by `glslc`
  (assumption #18). The long-term source language and DX12/Metal
  cross-compilation path (GLSL+SPIRV-Cross, HLSL+dxc, Slang or our own)
  is recorded as an open decision to settle before Phase 5's material
  system and no later than Phase 17.
- **2026-11-04 - Phase 4 visual milestone (Day 65).** A lit, textured, depth-tested
  cube renders through the Vulkan RHI on Wayland and on X11 (`game/rhi_demo`), with the
  validation layer and synchronization validation reporting nothing over 1000 frames on
  each backend. Measured on an NVIDIA GeForce RTX 3060 (driver 615.78.08) on a 240 Hz
  display: 4.1-4.2 ms/frame (vsync-bound), resident memory +0.4 MiB between frame 100
  and frame 1000. The offscreen golden-image tests (`tests/rhi/golden/`) cover the
  triangle, cube, textured cube and lit cube, each with analytic pixel checks that do not
  depend on the goldens. Changes from the plan along the way: shaders are GLSL compiled by
  `glslc` (assumption #18, decided Day 58); front faces are `CounterClockwise` with the
  Y-flipped projection (found by test on Day 63). The grep gate, hardening and the
  portability review remain (Days 66-67).
- **2026-11-04 - Phase 4 complete (Days 66-67).** The Vulkan boundary is enforced
  by the `rhi_boundary_check` ctest (comment-aware scan, reasoned allow-list, planted-
  violation fixtures); it found two real leaks of the boundary on its first run. Failure
  injection (`FaultInjection`: device loss at a frame, allocation failure) and a 200-resize
  storm are tested; sanitizers are green with a reasoned `lsan.supp`. The portability review
  walked every RHI concept against Direct3D 12 and Metal: one interface change (buffer-to-
  texture copy alignment is now a device cap, 512/256 by default), the rest are backend
  shims filed in the Phase 17/17 notes; the shader format and a mandatory clip-space Y flip in
  any shader translator are recorded as inputs to the open shader-toolchain decision.
  Baseline (RTX 3060, Release): ~0.5-0.6 us per draw recorded, 0.15-0.2 us per barrier,
  14-18 GiB/s uploads, cube pipeline 3.6-5.8 ms. Phase 5 still needs its day files.
- **2026-11-04 - Phase 5 shader toolchain decided: Slang.** Chosen for platform
  portability over GLSL+SPIRV-Cross and HLSL+dxc (assumption #18). Phase 5 is split
  into Days 68-84 under `plans/phase-05-renderer-core/`: deferred lighting (no
  MSAA in the RHI), a generic render graph on top of the RHI, and none of the
  deferred RHI extras (bindless, indirect draws, async compute).

- **2026-11-05 - Phase 5 complete (Days 68-84).** A render graph that owns every barrier and
  records passes in parallel, a material system on Slang reflection and permutations, a deferred
  pipeline (G-buffer, directional/ambient and point-light-volume lighting, ACES tonemap), the
  `renderer_demo` (540 objects, 300 lights, golden image, depth tests), failure injection at every
  stage of a frame, and `renderer_bench` (0.08 us per object, 16 draws for 10000 objects, 99% transient
  pool hit rate). Every shader compiles to HLSL and Metal (`shaders_portability`); the portability
  review lists 4 shims and no interface redesign. Windows (DX12) is next as Phase 6 and needs its day
  files first.

- **2026-11-05 - Three phases inserted before the Windows port: 5 Test scoping, 6 Debugging
  facilities, 7 RHI reset.** The user asked for (a) a way to re-run only the current phase's
  tests during development, with the full suite on phase completion (the suite has grown to
  about 1800 tests), (b) debugging facilities (stack traces, core dumps, minidumps,
  assert/ensure macros) and (c) a hard requirement that any rendering setting can change at run
  time by re-initializing the RHI, which also makes device loss recoverable. Test scoping goes
  first so the other two benefit from it. Everything from the old Phase 6 shifted by 3 (8 Windows,
  9 macOS, 10 Font, 11 UI, 12 ECS, 13 Physics, 14 Audio, 15 Assets, 16 Scripting, 17 Android + iOS,
  18 Consoles, 19-20 Unreal import, 21 Editor, 22 First playable demo, 23+ Feature parity); plan files
  were renamed and cross-references updated, while earlier log entries and dated specs keep the
  numbers they were written with. The three new phases are at goal level and need their day files
  before they start.

- **2026-11-06 - Phase 6 complete (Days 85-90).** Every one of the 1874 tests carries one `phaseN` label from
  `tests/phases.txt` (enforced by `test_phase_labels`); `scripts/test-phase.sh` runs the current phase (default), the
  current phase plus the modules a change touched (`--affected`), one phase, or everything, and always reports
  `ran X of Y tests`. Measured on the development machine: the whole suite in three flavors takes 66 s at `-j16` and
  341 s at `-j1`; a day that touches only the renderer and RHI selects 638 tests (26 s), a day in `engine/foundation`
  selects 871 (38 s), and a new phase's own tests run in about a second. Planted regressions confirmed the design: a
  foundation allocator break is caught by `--affected`; a break in the RHI is missed by `--current` and caught by
  `--affected` and `--all`. CURRENT_PHASE is now 6; Phase 7 (Debugging facilities) needs day files first.

- **2026-11-05 — UI phase scope (then Phase 7, now Phase 13): theming and UI render-to-texture.**
  Theming is a required deliverable (run-time-swappable themes, per
  subtree and per surface, named widget variants), not a stretch. A new
  "UI surfaces" section adds rendering a widget tree into an offscreen
  texture so UIs can appear on world objects such as screens: widgets
  must not assume a window target, and input arrives as an abstract
  event stream a surface can be fed (from a window, or from a ray hit on
  a mesh). The UI phase's definition of done now includes both. Plan-only
  change; that phase still needs its day files before it can start.

- **2026-11-05 — Windows and macOS ports moved up to Phases 6 and 7.**
  The user wants both desktop ports right after the renderer core instead
  of after Scripting. Phases were reordered and renumbered (files renamed,
  cross-references updated; dated design docs from before 2026-11-04 and
  earlier log entries keep the numbers they were written with):
  old 5→7 Font, 6→8 UI, 7→9 ECS, 8→10 Physics, 9→11 Audio, 10→12 Assets,
  11→13 Scripting, 12→5 Windows+DX12, 13→6 macOS+Metal. The iOS platform
  layer left the macOS phase and joined Android in Phase 19 ("Android and
  iOS ports"). Phases 18-25 keep their numbers. Hardware: a Windows PC and
  a 2019 Intel MacBook Pro (Intel/AMD GPU; Apple Silicon behavior stays
  unverified, see `plans/phase-23-macos-metal.md`). Open: how Phases 8-18
  keep the Windows/macOS builds working (assumption #11). Plan-only
  change; Phases 6 and 7 still need day files before they can start.

- **2026-11-06 — Phase 7 (Debugging facilities) planned.** Twelve day files, Days 91-102, under
  `plans/phase-07-debugging-facilities/`: design and spikes, death-test helper, assert/ensure/verify family, stack capture,
  symbolization, crash report content, the crash handler, core dumps, an own Linux minidump writer (crashing thread only)
  and reader, subsystem integration, and the closing run. Planning only; no code. Day 91 settles the open questions with
  spikes.
- **2026-11-06 — Phase 7 scope: Linux implemented, Windows and macOS made ready.** The Phase 7 plan now has 13 days
  (91-103). Linux is the only backend written; a new Day 102 does everything for the Windows (Phase 13) and macOS (Phase 22)
  backends that can be built and tested on Linux (Mach-O and PE module readers with fixtures, a backend conformance
  suite, cross-target syntax checks, per-backend `BACKEND.md` checklists kept in step by a test, a porting guide), so
  those phases write only the OS-call layer. Death tests re-execute the test binary instead of calling `fork`, and a
  gate keeps platform types out of the neutral headers. Mobile and consoles are designed in only. Planning only.

- **2026-11-06 — Phase 7 rebuilt on existing libraries (assumption #20).** Decided: do not hand-write the crash
  machinery. libbacktrace provides stack traces and symbols; a vendored crash-reporting library (proposed sentry-native
  with the Crashpad backend, no upload) provides crash capture and minidumps. The plan drops the own stack walker, ELF
  symbol reader, minidump writer and reader, and the Mach-O/PE readers, and becomes 12 days (91-102). Day 91 selects the
  libraries by spike. Planning only.
- **2026-11-06 — Phase 7 Day 91 done: libraries chosen by spike.** libbacktrace (commit `0b9b49cf4a`) and sentry-native 0.17.6
  with the **Breakpad** backend and no transport. The proposed Crashpad backend was rejected: on Linux it requires libcurl and
  its handler executable needs libcurl plus about 25 shared libraries. Assumption #20 and `CLAUDE.md` name the final choice;
  the spec is `docs/superpowers/specs/2026-11-06-debugging-design.md`.
- **2026-11-06 — Phase 7 (Debugging facilities) complete (Days 91-102).** Linux is implemented; Windows and macOS are ready (audit,
  `BACKEND.md` checklists kept in step with the headers by a test, conformance list, cross-target syntax checks, porting guide).
  Two deviations from the plan, both recorded in the day files: the report is written by a small signal recorder placed in front
  of Breakpad's handler on Linux (the library's `on_crash` hook gets no signal information there and runs on a helper thread);
  and the log ring needed a single-writer slot claim. Closing run: `ran 2371 of 2371 tests (phases: all; flavors: all; display:
  included)`, all passing; planted regressions all caught; both demos 1000 frames clean on Wayland and X11. `CURRENT_PHASE` stays
  6 until Phase 8 has its day files and starts.

- **2026-11-06 — Phase 8 (RHI reset) planned: Days 103-114.** Day files written under `plans/phase-08-rhi-reset/`. Seven design calls recorded in the phase file for Day 103's spec to confirm: a reset destroys and re-creates the `Device` (owner `rhi::DeviceHost`); handle generations are seeded per device so stale handles are detected; owners rebuild from CPU-side sources through a tiered registry; `backend` is a setting (Null to Vulkan tested, Vulkan to DX12/Metal left to Phases 13 and 22); fullscreen needs a new window API (Wayland and X11); a failed reset rolls back; settings are an open registry with declared cost classes. Planning only; `CURRENT_PHASE` stays 6 until Day 103.

- **2026-11-06 — Phase 8 (RHI reset) complete (Days 103-114).** A running application changes any rendering setting through `Renderer::ApplySettings`, which performs the cheapest sufficient action (none, dynamic, swapchain reconfigure, pipeline rebuild, device replacement through `rhi::DeviceHost`), at a frame boundary, with rollback to the previous settings; a lost device or surface recovers through the same path under a retry policy. Every device-owning type keeps its CPU-side source and rebuilds through a tiered registry (`CLAUDE.md` states the rule); handles of a destroyed device are always detected. Verified: 100 resets on the null backend and on the real device leave counts, device memory and descriptors unchanged; the demo picture is byte-identical before and after a reset and matches the golden; six of seven planted regressions caught (the seventh is an equivalent mutant). Measured on one AMD machine: a device reset takes about 40 ms. Deliberately not done: a settings system (the demos have no settings file or `--set`; filed as needed), the backend switch beyond Null to Vulkan (needs a backend registry; Phase 13), HDR10 and cross-adapter checks. The lifecycle checklist for DX12 and Metal is `engine/rhi/BACKEND-LIFECYCLE.md`, kept in step by a test, with a conformance suite. `CURRENT_PHASE` stays 7 until Phase 13 has its day files and starts.

- **2026-11-06 — Phases 9 and 11 inserted; the roadmap renumbered.** The settings system the demos need cannot be built on the scattered file code the engine has
  (`fopen`, `open`, `opendir`, `stat`, `mkdir`, `rename`, string-pasted paths in the foundation, the RHI, the demos, the tools and the tests). Two new phases:
  **Phase 9, File handling** (`io::Path`, the `io::File` interface with sync and async operations, `io::Directory`; every existing access site ported; an access
  gate; Days 115-124 written) and **Phase 11, Settings system** (goal level; its days are written after Phase 9 completes). Every later phase moved up by two:
  Windows and macOS are now Phases 13 and 22, font and text 12, UI 13, ECS 14, physics 15, audio 16, asset pipeline 17, scripting 18, Android/iOS 19, consoles 20,
  Unreal import 21 and 22, editor 23, first playable demo 24, feature parity 25. All references in plans, docs, comments and `CLAUDE.md` were renumbered the same
  way (the phase files are `plans/phase-NN-*.md` with the new numbers). `CURRENT_PHASE` stays 7 until Phase 9 starts.

- **2026-11-07 — Phase 9 (File handling) complete (Days 115-124).** One file and path layer: `io::Path` (value type, `PathStyle` so Windows rules are tested on
  Linux), `io::Error`/`Status` (errors are values), file-system operations and special folders, `io::File` (interface, `LocalFile`, a scheme registry for other
  backends), asynchronous operations over a foundation `IoExecutor` (the job system's I/O thread implements it; `co_await` adapters in `jobs`), and
  `io::Directory`/`ListDirectory`/`WalkDirectory`. A network-like test double passes the same conformance suite as the local file. Every file-access site in the
  foundation, jobs, the RHI pipeline cache, the demos and the tools was ported; the `file_access_check` gate fails on new direct file calls outside a documented allow
  list (the backend, the signal-safe and assert-path descriptor code, `memfd`, tests that plant raw shapes). `engine/foundation/io/BACKEND.md` is the Windows/macOS/
  mobile/console checklist, kept in step by a test, and the neutral headers compile for the Windows and macOS targets. Measured: an asynchronous read through the
  `IoExecutor` seam costs 2.32 us against 2.28 us directly on the I/O thread. Closing run: `ran 2914 of 2914 tests (phases: all; flavors: all; display: included;
  crash tests: included)`, all passing; both demos 1000 frames with validation on Wayland and X11. Deliberately not done (filed in `KNOWN-ISSUES.md`): memory
  mapping, file watching, share modes and locks, the Windows/macOS/mobile/console backends, network and pak backends. `CURRENT_PHASE` stays 9 until Phase 10 (standard types) and Phase 11 (settings
  system) have their day files and start.

- **2026-11-07 — Phase 11 (Settings system) planned: Days 130-139.** Day files written under `plans/phase-11-settings-system/`. Eleven design calls recorded in the phase
  file for Day 130's spec to confirm: the system lives in the foundation (`engine/foundation/settings/`); dotted keys; one shared registry with the renderer as a client;
  `ConfigStore` as an abstract class proven by a non-file store before the file store is written; layers with provenance and a write layer; cheap reads through a published
  snapshot; a damaged file is never overwritten by accident; a hand-written format that preserves comments and order; versioning with migrations and verbatim unknown keys;
  listeners on the changing thread; a check that keeps ad hoc configuration from creeping back. Planning only; `CURRENT_PHASE` stays 9 until Day 125 starts Phase 10 and Day 130 starts this phase.

- **2026-11-07 — Phase 10 (Standard types and container hygiene) inserted and planned: Days 125-129; the settings system became Phase 11 (Days 130-139) and every later phase moved up by one.** Raised after Phase 9: the engine runtime was already free of `std::` containers, but `game/`, the test helpers Phase 9 added and
  `tools/shader_mint` (outside its JSON boundary) were not, there was no standard `byte` and no 128-bit integer. Decisions made with the user: `byte` is an alias of `u8` plus `Span`/`ByteSpan` views; `i128`/`u128` are portable structs (MSVC has no
  `__int128`), tested against `__int128` and a forced-portable build; the rule is enforced in `engine/`, `game/` and `tools/` by a gate, and the older tests (about 1000 uses) are a filed deferral. Phase 11 (Days 130-139) follows it unchanged. Planning only;
  `CURRENT_PHASE` stays 9 until Day 125.

- **2026-11-07 — Rule: no decimal phases; renumber when the plan changes (twice).** The standard-types phase was first planned as "Phase 8.5" so the settings system would keep its number; the user rejected that, and then asked for the
  documentation tooling (the only other decimal, "Phase 2.5") to become Phase 3. Phases are whole numbers; an inserted phase renumbers everything after it, in plans, docs, code comments, tests and `CLAUDE.md`, in the same commit. Final
  numbering: 0 foundation, 1 Linux platform, 2 jobs, **3 documentation tooling**, 4 Vulkan RHI, 5 renderer core, 6 test scoping, 7 debugging, 8 RHI reset, 9 file handling, **10 standard types and container hygiene**, **11 settings system**, 12 Windows/DX12,
  13 macOS/Metal, 14 font and text, 15 UI, 16 ECS, 17 physics, 18 audio, 19 asset pipeline, 20 scripting, 21 Android/iOS, 22 consoles, 23 Unreal asset import, 24 Unreal Blueprint import, 25 editor, 26 first playable demo, 27 feature parity. The settings days are 130-139,
  the standard-types days 125-129. Test labels followed (`phase3` is the documentation tooling, `phase9` the file layer), `CURRENT_PHASE` is 9, and `tests/phases.txt`, `tests/module_map.txt` and the test-phase tooling only accept whole numbers.

- **2026-11-07 — Phase 10 (Standard types and container hygiene) implemented, Days 125-129.** `byte`, `isize`, portable-or-native `u128`/`i128` (checked against `__int128` on millions of random operations, native and forced-portable builds), `Span`/`ByteSpan`, `algorithm.h` (`Min`, `Max`, `Clamp`, `IndexOf`, `Sort` as introsort, `StableSort`), `Append`/`Assign`, and span overloads in the file layer; `engine/` and `game/` spell bytes `byte`/`u8`; `rhi::Span` became the foundation `Span<const T>`; `game/`, `tests/io`, the permutation tests and `tools/shader_mint` outside `reflect_json.cpp` use the engine's types; the `std_container_check` gate (with planted-violation self-tests and an allow list with reasons) keeps it that way, scanning every test directory and allow-listing the six older ones with counts. Not done: the older tests' conversion (a filed deferral), regenerating shaders with `slangc` (not installed here), building the portable 128-bit path with MSVC (Phase 13). Closing run `ran 3030 of 3030 tests (phases: all; flavors: all; display: included; crash tests: included)`, known X11 failure only. Phase 11 (settings) follows.

- **2026-11-07 — Phase 11 (Settings system) implemented, Days 130-139.** One registry (`settings::Registry`) that every module declares settings in, typed `Setting<T>` handles, the `ConfigStore` interface with memory, read-only and file stores (comment-preserving text format, atomic saves, `.bak`, damaged-file protection), a layer stack with provenance (system, user, project, custom, environment, command line), the `Settings` service (apply with rollback, listeners, snapshots, autosave, crash-state), schema versions with migrations and compatibility mode, the renderer's settings as a client, the engine and log settings, `--set`, `--settings`, `--save-settings`, `--list-settings` and generated `--help` in both demos, platform checklist `engine/foundation/settings/BACKEND.md` with a registry-shaped test store, and the `config_access_check` gate (assumption #22). The end-of-phase review (`--max-findings all`) found 18 issues: 14 groups fixed with tests, 4 filed. Closing run `ran 3441 of 3441 tests (phases: all; flavors: all; display: included; crash tests: included)`, known X11 failure only; both demos 1000 frames with validation on Wayland and X11. Not done (filed in `KNOWN-ISSUES.md`): file watching, a UI binding layer, the Windows/macOS/mobile/console stores, list-valued settings. `CURRENT_PHASE` stays 11 until Phase 13 has its day files and starts.

- **2026-11-07 — Phase 13 (Windows port + DX12) planned, Days 147-174.** Decided with the developer: MSVC is the primary Windows compiler with a `clang-cl` build kept compiling (PDBs, a DbgHelp resolver behind `ResolveStackTrace`; libbacktrace stays Linux-only); the CMake toolchain files locate MSVC, the Windows SDK and `clang-cl` themselves (`vswhere`, the Windows Kits registry key), so no Developer Command Prompt is needed; the existing Vulkan backend is ported to Windows before DX12 is written (it proves the window and renderer and gives the same-machine baseline); Linux stays verified on the separate Linux machine on every day that changes shared code; Windows runs the plain and ASan flavors (MSVC has no TSan/UBSan). Also settled while planning: the UCRT is allowed (assumption #2; the phase file's old "no CRT" wording was wrong), a backend registry and format-tagged shader blobs come before DX12, DXIL is minted by `slangc` and committed, the D3D12 baseline is feature level 12_0 / SM 6.0 / binding tier 2 / legacy barriers with no Agility SDK, golden images are never re-blessed for a platform, and line endings are pinned with `.gitattributes`. `CURRENT_PHASE` stays 11 until Day 147 starts.

- **2026-11-07 — Phase 12 (Module layering) inserted before the Windows port, Days 140-146; everything after it renumbered by one.** Measuring the code for `ARCHITECTURE.md` showed nine parts of the foundation in one dependency cycle (six upward includes: the assert failure path into debugging, the log's config file and the I/O thread's crash registration into the file layer and debugging, `os.h` into strings, the window code compiled into the foundation, a program in a library directory), which is why `foundation` is one library and why the Windows plan could not test a layer before all were ported. The developer chose to fix it in its own phase first. New Phase 12: a `module_deps_check` gate with today's violations allow-listed, hooks for the assert report and thread registration, `os.h` below strings, windows and input moved to `engine/platform/`, one library per layer. Renumbered in one pass: Windows/DX12 is now Phase 13 (Days 147-174), macOS 14, font 15, UI 16, ECS 17, physics 18, audio 19, assets 20, scripting 21, Android/iOS 22, consoles 23, Unreal assets 24, Blueprints 25, editor 26, first playable demo 27, feature parity 28; lines that record earlier renumberings were left as written. The Phase 13 days now depend on the layered libraries and test each layer when its backend is ported.
- **2026-11-07 — Phase 12 (Module layering) implemented, Days 140-146.** No include cycle and no upward edge (`module_deps_check`, 136 module edges, 0 problems); the only allow-list entries are the two RHI ones for Phase 13 Day 162. Assert reports and thread registration are hooks installed by `debug` (`log/diagnostics_hooks.h`); `StringView` is a base-layer type; `os::EnvironmentEntry` fills a caller buffer; `log::LoadConfig` is retired; `engine_main` moved to `game/hello_window/`; windows and input moved to `engine/platform/` (library `platform`, namespace still `os::`); the foundation builds as `foundation_base`, `foundation_io`, `foundation_debug` and `foundation_settings` with the `foundation` aggregate. Behavior is unchanged and build times are unchanged. `ARCHITECTURE.md` section 2 is the measured graph.

- **2026-11-08 — Phase 13 (Windows port + DX12 backend) complete, Days 147-174.** Windows x64 builds with MSVC and clang-cl and runs every test (plain and ASan), through Vulkan and Direct3D 12; the backend is chosen at start. The Linux machine's closing run: all four builds (Clang and GCC, unity on and off) clean, `ran 3606 of 3606 tests (phases: all; flavors: all; display: included; crash tests: included)`, docs 4492 of 4492 identical on both machines, both demos 1000 frames with validation on Wayland and X11. `CURRENT_PHASE` stays 13 until Phase 14 has its day files.

- **2026-11-08 — macOS + Metal port postponed (was Phase 14, now Phase 22, just before Android + iOS, which reuses its Metal backend); 3D text and colored glyphs split out of Phase 14 into a new Phase 16.** Final numbering after both changes: font and text 14, UI 15, 3D text and colored glyphs 16, ECS 17, physics 18, audio 19, asset pipeline 20, scripting 21, macOS 22, Android/iOS 23, consoles 24, Unreal asset import 25, Blueprint import 26, editor 27, first playable demo 28, feature parity 29. Plan files, `PLAN.md`, `CLAUDE.md`, `STATUS.md`, the memory notes, the docs and every comment that names a phase were renumbered in the same commits. macOS stays designed in (the RHI, platform and settings seams, the `BACKEND.md` checklists and the macOS cross-syntax checks keep running); nothing macOS-specific is built until Phase 22. Audio (Phase 19) is Linux and Windows until then. Phase 14 is cut to what "hello world" needs (FreeType, one font, atlas, layout, an overlay text pass, grapheme-aware cursor and hit-testing).

- **2026-11-09 — Phase 14 (Font & text rendering) complete on Linux, Days 175-183.** `engine/text` (FreeType 2.15.0 behind one file, `FontLibrary`/`Font`, glyph rasterization, shelf-packed atlas, layout with kerning and wrapping, grapheme-aware cursor and hit-testing) and, in the renderer, `GlyphAtlasTexture` and `TextRenderer` (text shaders in Slang, SPIR-V; a `text` pass after the tonemap; reset- and loss-safe), shown by `renderer_demo --text` (F11) with a golden. The Linux closing run: `ran 3789 of 3789 tests (phases: all; flavors: all; display: included; crash tests: included) in 143 s` (the known X11 window flake failed once), `ctest -L docs` 6 of 6, both demos 1000 frames with validation on Wayland and X11, clean. **Open: the Windows PC has to build and run everything and mint DXIL for the text shaders** (`KNOWN-ISSUES.md`). `CURRENT_PHASE` stays 14 until Phase 15 has its day files.

- **2026-11-10 — Phase 15 (UI system) broken into Days 184-206.** Twenty-three day files: design spec and spikes, a CPU-only `engine/ui` module beside the RHI, the platform input event stream, pointer lock, clipboard and drag and drop on Wayland and X11, the draw list and `renderer::UiRenderer`, themes, widget tree/layout/input routing, the widget set, popups and menus, the viewport widget, UI surfaces (render-to-texture), settings screen and debug overlay, `ui_demo`, one Windows-PC day (Win32 events, DXIL), and the closing day. Nothing started; `CURRENT_PHASE` stays 14.

- **2026-11-09 — Phase 15 (UI system) complete on Linux, Days 184-206 (Day 205 on the Windows PC).** `engine/ui` (CPU toolkit: surface, widget tree, layout, themes with subtree overrides, input routing, popups and modality, drag and drop, clipboard, every widget family, viewport widget with mouse look, UI surfaces rendered into textures with ray-cast input, settings screen, debug overlay), `engine/ui_window`, `renderer::UiRenderer` and the Slang UI shaders (SPIR-V and DXIL), the platform input event stream, pointer lock, cursor shapes, OS clipboard and drag and drop on Wayland, X11 and Win32, and `game/ui_demo` with six goldens. The Linux closing run: `ran 4952 of 4952 tests (phases: all; flavors: all; display: included; crash tests: included) in 147 s`, `ctest -L docs` 7 of 7, the three demos 1000 frames with validation on Wayland and X11, clean. The uncapped end-of-phase review (five area reviewers, about 66 candidates) fixed the real defects (an unbounded loop in `SettingsView::Rebuild`, unclamped `snprintf` lengths, a dangling tab reference, theme changes that missed style padding, Wayland and X11 input and clipboard state) and filed the rest. **Open:** no human has used the UI in a live window beyond the Windows DPI check, and the Windows PC has not run the closing suite after Day 206 (`KNOWN-ISSUES.md`). `CURRENT_PHASE` stays 15 until Phase 16 has its day files.

- **2026-11-09 — Phase 16 inserted: standard-library wrappers (Days 207-221).** On the user's request (an audit of every standard-library use, all wrapped so that only wrapper modules use `std::` directly) the plan gains a phase before 3D text: modules `lang`, `sync`, `cstr` (new) and wrappers in `mem`, `os`, `jobs`; a port of `engine/`, `game/`, `tools/` and `tests/`; a Windows-PC day; the `std_usage_check` gate replacing `std_container_check`. Phases 16-29 became 17-30 (plan files, roadmap, assumptions, STATUS, CLAUDE.md, BUILD.md, KNOWN-ISSUES, comments). Assumption #23 states the rule; the day files are written; nothing started, `CURRENT_PHASE` stays 15.
- **2026-11-10 — Phase 17 broken into day files (Days 222-237).** Geometry first (flatten, cleanup, triangulate), large 2D text replaces the scaled-raster stand-in, extruded and beveled 3D text through the renderer, then colored glyphs: COLR v0 via FreeType, and `CBDT`/`sbix` via our own table readers and an engine-owned inflate/PNG decoder (`engine/image`; no libpng). Nothing started; `CURRENT_PHASE` stays 16 until Day 222.
