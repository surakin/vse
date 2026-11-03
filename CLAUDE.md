# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

See `STATUS.md` for the current snapshot — what's built, what's next,
and the build/test commands. **`STATUS.md` must be updated in the same
commit whenever the plan advances**: a day file's tasks get checked off,
a phase starts or completes, or a build/test command changes. Treat an
implementation commit that doesn't also update `STATUS.md` as incomplete.

## Planning policy

The user has explicitly stated: **do not write code until the plan is
settled.** Treat `PLAN.md` and the `plans/` directory as the current
source of truth for what "settled" means, and check with the user before
starting implementation if it's ambiguous whether planning is done.
"Settled" means Phase 0 is detailed enough to start — it does not mean
the plan is finished or will stop changing. PLAN.md is explicit that this
is a living document: expect scope, architecture calls, and even phase
boundaries to keep changing throughout the project as implementation
reveals what the plan got wrong, not just during this initial pass.

**This settledness bar applies at every phase transition, not just
Phase 0.** No phase may be started — no code, no further sub-work within
it — until every day of that phase has its own file under
`plans/phase-NN-name/day-NN-slug.md`, broken into specific, individually
checkable sub-tasks with a concrete Done condition, and committed to the
repo. A phase left at goal/deliverable level, or one whose days are still
single undifferentiated paragraphs rather than files of specific
sub-tasks (see "How the plan is structured" below), is not yet
startable; doing that breakdown is itself planning work that has to
happen, and be committed, first.

## End-of-day code review (required)

**Every implementation day ends with a high-effort code review before it
is considered done.** After a day's code and tests are written and
passing, and before the final commit for that day:

1. Run `/code-review high` on the day's diff (the uncommitted changes,
   or the day's commit range if it was already committed in pieces).
   Never use the `max` or `ultra` levels unless the user asks for one.
2. Treat every finding seriously: verify it, fix it, and add a
   regression test where one is practical (confirm the new test actually
   fails without the fix). A finding that is declined must be recorded
   with the reason, not dropped silently.
3. Record the outcome in the day file under a "Post-implementation code
   review" section — what was found, what was fixed, what was declined
   and why — and mention it in `STATUS.md`.
4. Only then commit the day as complete. If the review's fixes land in
   follow-up commits, the day is not done until they do.

This applies to every implementation day in every phase, including days
that look low-risk. It does not apply to planning-only commits (plan
files, `STATUS.md`-only edits). Concurrency, lifetime/ownership and
memory-management code deserves extra scrutiny during the review — the
reviews so far found real bugs in exactly those areas that the
sanitizers and stress tests had not (dropped Wayland seat capabilities,
a dead-connection hang, a thread-local destructor-order leak, a
sanitizer canary that could pass for the wrong reason). Sanitizer and
stress-test runs in a day file are verification of behavior, not a
substitute for this review.

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
  containers, and string handling instead of reaching for
  malloc/printf/string.h/the C++ STL — for control, performance, and as a
  learning exercise, not because linking libc is banned outright. Scalar
  math functions (`sqrt`/`sin`/`cos`/`acos`/`atan2`/etc.) and concurrency
  primitives (`std::thread`, `std::mutex`, `std::condition_variable`,
  `std::atomic`, C++20 coroutines) are used directly rather than being
  hand-rolled or wrapped in a custom engine type — solved problems, and
  (for concurrency) already-portable across every target platform, with
  no control/performance/learning upside to reinventing or re-wrapping
  them; the vector/matrix/quaternion *types* built on top of `<cmath>`
  are still hand-rolled. See `PLAN.md` assumption #2. STL containers/
  algorithms, exceptions, and RTTI remain unused/disabled.
- **The engine is heavily multithreaded, through one job system — not ad
  hoc thread pools per subsystem.** Phase 2 defines a portable
  work-stealing job scheduler that every later multithreaded subsystem
  (ECS parallel system scheduling, renderer command recording, physics,
  audio, asset streaming) schedules work through. Jobs are C++20
  coroutines, not stackful fibers — fiber context-switching needs
  per-ABI code (hand-written assembly or a wrapped OS-native facility),
  which conflicts with this layer's whole point of being portable; see
  `PLAN.md` assumption #15.
- **RHI abstraction is mandatory.** Renderer/gameplay code must never call
  Vulkan, DX12, or Metal directly — only through the common RHI interface
  defined in Phase 3. Vulkan is the first backend; DX12/Metal follow later
  behind the same interface.
- **Data-oriented ECS**, not an OOP scene graph.
- **Linux first, Wayland primary / X11 fallback.** Every system is built
  and proven on Linux before porting to Windows/macOS/Android/iOS/consoles
  (Phases 12-15). On Linux, Wayland is the primary display backend, X11 is
  a fallback backend for systems that haven't moved to Wayland — both
  built on their reference client libraries (`libwayland-client`,
  `libX11`/`libxcb`) rather than a hand-rolled wire-protocol client.
- **Every target platform is designed in from day one — desktop, mobile,
  and console alike — even though only Linux is implemented before
  Phase 12.** Platform-layer and engine-facing interfaces must stay
  shaped so no later backend (Phases 12-15) ever forces a redesign of
  already-built engine layers — see `PLAN.md` assumption #11. Build order
  still follows the roadmap (Linux → Windows/macOS → Android/iOS →
  consoles); that's not license to design as if mobile/console don't
  exist until their phase starts.
- **Font rendering is backed by FreeType** (Phase 5), not a hand-rolled
  rasterizer. Deliberate, individually justified exception to the
  third-party-dependency line — the same category as Lua/LuaJIT below,
  ships inside the runtime engine/game binary — see `PLAN.md` assumption
  #17. Vendored as source under `third_party/freetype/` (built from
  source, same pattern as Catch2) rather than linked against a
  distro-provided `libfreetype.so`, since unlike `libwayland-client`/
  `libX11` it isn't guaranteed present on every target platform
  (Android/iOS/consoles). FreeType handles font parsing and glyph
  rasterization only — text shaping for complex scripts (HarfBuzz's
  domain) is explicitly out of scope until a real need shows up; cursor/
  line-break positions instead reuse the foundation string module's
  existing grapheme clustering.
- **Gameplay scripting embeds real languages, not a custom bytecode VM —
  starting with Lua via LuaJIT** (Phase 11). Deliberate, individually
  justified exception to the third-party-dependency line, ships inside
  the runtime engine/game binary — see `PLAN.md` assumption #13. Designed
  with a pluggable language-backend seam so more languages can be added
  later without a redesign. Phase 17's Blueprint importer targets this
  (transpiling to Lua) instead of a custom VM.
- **Unreal Engine project import is a headline objective**, not a nice-to-
  have (Phases 16 and 17): loading a real, unmodified UE 5.3+ project's
  meshes/textures/materials/levels, and eventually Blueprint gameplay
  logic, directly into this engine. Deliberately sequenced *after*
  multiplatform support (Phases 12-15) rather than right after the asset
  pipeline — see `PLAN.md` assumption #14. Targets uncooked editor-format
  projects, not shipped/cooked games. The offline import tool links a DLL
  built from Unreal Engine's own source to parse `.uasset`/`.umap`
  content via UE's real `UObject`/package-loading system, rather than a
  from-scratch reverse-engineered parser — scoped to the import tool
  only, never linked or shipped in the engine/game runtime (see
  `PLAN.md` assumption #6). **Open risk, unresolved:** Epic's UE EULA
  terms need review before Phase 16 work starts — flagged in `PLAN.md`,
  not yet resolved.
- **The GUI is the engine's own widget toolkit, not a third-party one**
  (Phase 6): a retained-mode widget set (menus, buttons, comboboxes,
  scrollbars, labels, text fields, ...) built on the RHI and the Phase 5
  text layer, including a **viewport widget** that hosts a rendered 3D
  world. No Qt/GTK/Dear ImGui-style dependency — that would breach the
  zero-third-party-convenience-dependencies rule. In-game UI, debug
  tooling and the editor all share it.
- **Engine core before editor.** The editor (Phase 18) is built last,
  using the engine's own renderer/ECS and Phase 6 UI system — not a
  separate GUI toolkit.

## How the plan is structured — follow this when doing planning work

- `PLAN.md` is the index only: mission, principles, architecture layers,
  repo layout, the phase roadmap table, and a progress log. It does not
  contain day-by-day tasks.
- Each `plans/phase-NN-*.md` file owns one phase and is itself an index:
  Depends-on/Goal/Definition-of-done, a list of links to that phase's day
  files in order, and Notes/open questions. It does not contain the tasks
  inline.
- **Each day-sized task gets its own file**, under
  `plans/phase-NN-name/day-NN-slug.md` (e.g.
  `plans/phase-00-foundation/day-04-arena-allocator.md`). A day file
  contains that day's specific, individually checkable sub-tasks (not
  one paragraph covering the whole day) and ends with a concrete
  **Done condition** — a day without one isn't finished, and a day whose
  sub-tasks aren't broken out individually isn't detailed enough to
  start either.
  Near-term phases (00, 01, 02) are the ones expected to have day files;
  all three are written this way. Later phases are intentionally left
  at deliverable/goal level rather than speculatively detailed far in
  advance — but that breakdown
  (writing every day file) has to happen, and be committed, before the
  phase starts, not during or after (see "Planning policy" above). "To be
  expanded into day-by-day tasks when started" means expand first, then
  start — not start and figure out the tasks along the way.
- Tasks are checked off (`[x]`) in place as completed, in their own day
  file. Completed work is not reordered or deleted — it's the project's
  history.
- If a task turns out wrong-sized, blocked, or based on a bad assumption,
  stop and edit the relevant plan file before continuing rather than
  silently drifting from it.
- The intended build order follows the phase numbering in `PLAN.md`'s
  roadmap table (dependencies are also noted at the top of each phase
  file) — don't skip ahead to a later phase's work without checking its
  stated dependencies are actually done.
