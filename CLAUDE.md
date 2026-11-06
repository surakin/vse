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

## Claude memory lives in the repo

The auto-memory files are `claude-memory/` in this repo, so they move with
the checkout between machines. Claude Code looks for them under
`~/.claude/projects/<project path>/memory`, which is a symlink to that
directory; on a new machine run `scripts/link-claude-memory.sh` once. Memory
edits show up as ordinary changes under `claude-memory/` and are committed
and pushed with the work that prompted them.

## Known issues are tracked (required)

`KNOWN-ISSUES.md` lists failing tests, flaky tests, checks no human has done
yet, and items deferred to a later phase. **Update it in the same commit
whenever a day leaves something in one of those states, and remove the
entry in the commit that fixes it.** That covers a test you saw fail or
flake (even if unrelated to the day's work), a measurement you chose not to
take, and a limitation filed for a later phase. Record observed facts and
where they were seen; do not guess at causes. Do not leave such an item only
in a day file or in `STATUS.md` prose.

## Tests run by phase; the full suite closes a phase (required)

Every test carries one phase label (`tests/phases.txt`, enforced by `test_phase_labels`), and `CURRENT_PHASE` names the
phase in progress. Day to day, run only what the work can have affected:

- `scripts/test-phase.sh --affected` (plain flavor) runs the current phase plus the phases of every module the work
  touched; it runs everything by itself when a `CMakeLists.txt`, `cmake/`, `third_party/`, an `engine/foundation`
  header or the phase map changed. Before the end-of-day review, also run the sanitizer flavors of the same selection
  (`--affected --flavor asan`, then `--flavor tsan`) when the day touched concurrency, lifetime or memory code.
- **A reduced run is reported as what it was**: the day file and `STATUS.md` quote the script's final line
  (`ran X of Y tests (phases ...)`) and never say "all tests pass" for a run that left tests out.
- **A phase's last day runs the whole suite in every flavor**: `scripts/test-phase.sh --all --flavor all`, plus
  `ctest -L docs` and the demos the phase ships, and records the line in the day file. A phase is not complete without
  it. `CURRENT_PHASE` changes only in the commit that starts the next phase.
- A new test file needs a line in `tests/phases.txt` (the build stops without one); any other `add_test` needs a
  `test:` line there and `engine_label_tests(<name>)`. `ctest -L docs` is not scoped: run it on every day that changes
  a public header or a guide.
- A flaky test is recorded in `KNOWN-ISSUES.md` with the phase it belongs to, so a scoped run that skips it is not
  mistaken for a fix.

See `docs/guide/building-and-testing.md` ("Test phases and scoped runs").

## Unity builds: fix conflicts with macros first, exclude files last (required)

`ENGINE_UNITY_BUILD` (root `CMakeLists.txt`, default ON) merges each target's sources into a few translation units.
When a file breaks under it, work down this list and stop at the first step that fixes it:

1. **Surround the problematic `#include` with an enable/disable pair** that keeps the header's macros from leaking
   into the files merged after it. `unity/x11_enable.h` (before the X11 includes) saves the clashing macro names with
   `#pragma push_macro`; `unity/x11_disable.h` (after the file's last use of them) restores them. Another leaky
   header gets its own `unity/<name>_enable.h` / `_disable.h` pair in `engine/foundation/unity/`, same shape.
2. **Rename or share colliding file-local symbols** (anonymous-namespace helpers with the same name in two files):
   give them distinct names or move the common helper to a header.
3. **Only then** exclude the file with `SKIP_UNITY_BUILD_INCLUSION`, and list it in `KNOWN-ISSUES.md` with the
   reason steps 1-2 did not work.

Never skip a file from unity builds as the first response to a conflict. The plain `-DENGINE_UNITY_BUILD=OFF` build
must keep passing too, so every source still compiles on its own.

## End-of-day code review (required)

**Every implementation day ends with a high-effort code review before it
is considered done.** After a day's code and tests are written and
passing, and before the final commit for that day:

1. Run the day's scoped tests first (previous section), then `/code-review high` on the day's diff (the uncommitted changes,
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

## Public API is documented before a day is done (required)

**A day that adds or changes public API is not complete until it is
documented.** "Public API" is anything in a header named by
`docs/reference/modules.txt`. Before the final commit for such a day:

1. The header carries `///` comments per `docs/DOC-CONVENTIONS.md`
   (summary, plus the contract labels the module's `strict` rules demand),
   and `docgen lint` passes with the baseline at zero known findings —
   no new undocumented symbol is parked in the baseline.
2. `cmake --build build --target docs` has been run and its output
   (`docs/reference/`) is committed with the code.
3. The relevant guide page under `docs/guide/` is updated or created, and
   any tagged code samples in it compile and run.
4. The day file's Done condition names the pages touched, and
   `ctest -L docs` passes.

Contract labels name the macro family: `Aborts:` for a function with an `ENGINE_ASSERT`, `ENGINE_VERIFY` or `ENGINE_CHECK_SLOW`
precondition, `Fails:` for a failure reported by return value or by an `ENGINE_ENSURE` the function survives. A function a
platform backend must implement (the debugging facilities) also carries `Backend:` and a section in both
`engine/foundation/os/windows/debug/BACKEND.md` and `os/macos/debug/BACKEND.md`; the ctest `debug_backend_lists` checks it.
Code that runs inside a crashing process sits between `// SIGNAL-SAFE-BEGIN` and `// SIGNAL-SAFE-END` and may not allocate, lock
or format with the C library (`tools/signal_safety_check` enforces it).

New modules are added to `docs/reference/modules.txt` in the same day.
Planning-only commits are exempt, as are days that change no public header.

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
  and proven on Linux before porting to Windows/macOS/Android/iOS/consoles.
  Windows and macOS are Phases 8 and 9, right after the renderer core
  (Phase 4), and port what exists by then (platform layer, RHI, renderer,
  shaders); Android/iOS and consoles are Phases 17-18. How the later
  Linux-first phases (7-13) keep the Windows/macOS builds working is an
  open decision, see `PLAN.md` assumption #11. On Linux, Wayland is the primary display backend, X11 is
  a fallback backend for systems that haven't moved to Wayland — both
  built on their reference client libraries (`libwayland-client`,
  `libX11`/`libxcb`) rather than a hand-rolled wire-protocol client.
- **The RHI can be reset at run time (Phase 7).** Users must be able to change any rendering setting without restarting
  the app, so re-initializing the device, swapchain and every device-dependent resource is a supported operation, not
  a recovery hack. Anything that owns GPU resources must therefore be able to rebuild them (keep its CPU-side source
  or a reload path), and a lost device recovers through the same path. Do not add a GPU-owning type that cannot be
  recreated. The concrete rule (`docs/guide/renderer.md`, "Surviving a reset"): an owner of device objects **keeps its CPU-side
  source, registers with `renderer::DeviceResources` at its tier (a `ScopedRegistration` member), implements `Release`/`Recreate`,
  and never caches a handle past a frame** (hold the owner and read `Handle()` when recording). Materials bind textures by owner,
  not by raw handle. A test of such an owner resets the device through `tests/renderer/reset_rig.h` and compares the counts from
  `Device::Resources()` (`docs/guide/building-and-testing.md`, "Testing a reset"). Rendering settings change at run time through
  `Renderer::ApplySettings` and are classified by cost (`docs/guide/render-settings.md`).
- **Every target platform is designed in from day one — desktop, mobile,
  and console alike — even though only Linux is implemented before
  Phase 8 (Windows).** Platform-layer and engine-facing interfaces must stay
  shaped so no later backend (Phases 8-9 and 17-18) ever forces a redesign of
  already-built engine layers — see `PLAN.md` assumption #11. Build order
  still follows the roadmap (Linux → Windows/macOS → Android/iOS →
  consoles); that's not license to design as if mobile/console don't
  exist until their phase starts.
- **Font rendering is backed by FreeType** (Phase 10), not a hand-rolled
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
  starting with Lua via LuaJIT** (Phase 16). Deliberate, individually
  justified exception to the third-party-dependency line, ships inside
  the runtime engine/game binary — see `PLAN.md` assumption #13. Designed
  with a pluggable language-backend seam so more languages can be added
  later without a redesign. Phase 20's Blueprint importer targets this
  (transpiling to Lua) instead of a custom VM.
- **Unreal Engine project import is a headline objective**, not a nice-to-
  have (Phases 19 and 20): loading a real, unmodified UE 5.3+ project's
  meshes/textures/materials/levels, and eventually Blueprint gameplay
  logic, directly into this engine. Deliberately sequenced *after*
  multiplatform support (Phases 8-9 and 17-18) rather than right after the asset
  pipeline (Phase 15) — see `PLAN.md` assumption #14. Targets uncooked editor-format
  projects, not shipped/cooked games. The offline import tool links a DLL
  built from Unreal Engine's own source to parse `.uasset`/`.umap`
  content via UE's real `UObject`/package-loading system, rather than a
  from-scratch reverse-engineered parser — scoped to the import tool
  only, never linked or shipped in the engine/game runtime (see
  `PLAN.md` assumption #6). **Open risk, unresolved:** Epic's UE EULA
  terms need review before Phase 19 work starts — flagged in `PLAN.md`,
  not yet resolved.
- **The GUI is the engine's own widget toolkit, not a third-party one**
  (Phase 11): a retained-mode widget set (menus, buttons, comboboxes,
  scrollbars, labels, text fields, ...) built on the RHI and the Phase 10
  text layer, including a **viewport widget** that hosts a rendered 3D
  world. No Qt/GTK/Dear ImGui-style dependency — that would breach the
  zero-third-party-convenience-dependencies rule. In-game UI, debug
  tooling and the editor all share it.
- **Shaders are authored in Slang and compiled offline, never at
  runtime** (`PLAN.md` assumption #18). Sources are `shaders/*.slang`,
  compiled by `slangc` (dev-time, optional) to SPIR-V and embedded as
  committed `*.spv.inc` files; a checkout without `slangc` builds from
  the committed outputs. Don't add GLSL or hand-written SPIR-V. DX12 and
  Metal output comes from the same Slang sources in Phases 8-9 (note
  the clip-space Y flip and Metal's `main` rename in `shaders/README.md`).
- **Debugging facilities are built on existing libraries** (Phase 6): libbacktrace (stack traces and symbols) and
  sentry-native 0.17.6 with its Breakpad backend and **no upload** (`none` transport, crash capture and minidumps only) - a
  deliberate, individually justified exception chosen by spike (`PLAN.md` assumption #20,
  `docs/superpowers/specs/2026-11-06-debugging-design.md`). Vendored as source under `third_party/` and used only through
  the facade in `engine/foundation/debug/`: only its two implementation files include the libraries' headers, enforced by a
  gate. The assert family, crash context, death-test helper and core-dump scripts stay ours. Do not hand-write what these
  libraries already do, and do not add a network path.
- **Offline tools may use a vetted third-party library** where the job is
  solved and not engine-differentiating - today only nlohmann/json
  (vendored under `third_party/nlohmann_json/`) in `tools/shader_mint`,
  to read `slangc` reflection (`PLAN.md` assumption #19). Never linked
  into the engine, a game, the renderer or an engine test target (the tool's own tests link it); anything else
  needs its own entry in `PLAN.md`. That tool and its tests may use `std::string`/`std::vector` (the library
  requires them); the engine's runtime libraries still do not.
- **Terminology: we mint assets, we do not cook them.** Offline
  asset/shader processing tools are "mints"/"minters" (`tools/shader_mint`,
  the Phase 15 asset minter) and their outputs are "minted". "Cook" appears
  only when describing Unreal's pipeline. Minting must be deterministic:
  same inputs and tool version give byte-identical outputs, tested for
  every minter (see `PLAN.md`, guiding principles).
- **Engine core before editor.** The editor (Phase 21) is built last,
  using the engine's own renderer/ECS and Phase 11 UI system — not a
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
