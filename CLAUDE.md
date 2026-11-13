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

## Module dependencies follow ARCHITECTURE.md (required)

`ARCHITECTURE.md` lists every module, its layer and what it may depend on, measured from the code. **A module includes and links only modules
below it, never one above, and no cycles.** The gate `module_deps_check` (`tools/module_deps_check`; ctest `module_deps_check`) enforces it: `modules.txt` maps files and CMake targets to modules,
`allowlist.txt` holds the known violations with the day that removes each, and a stale entry fails. A change that adds a module, moves one or adds an allowed edge updates `ARCHITECTURE.md` in the same
commit; a new upward edge or cycle is not added, even temporarily. The known violations are listed there with their fixes.

## Build prerequisites are listed in BUILD.md (required)

`BUILD.md` lists what each platform needs installed (required, for checks, optional), with the versions in use. **A change that adds, removes or
re-versions a prerequisite updates `BUILD.md` in the same commit**: a new system library, tool, SDK, optional Windows feature or pinned tool version
(such as `slangc`). A phase that ports a platform adds that platform's section before its first implementation day.

## Claude memory lives in the repo

The auto-memory files are `claude-memory/` in this repo, so they move with
the checkout between machines. Claude Code looks for them under
`~/.claude/projects/<project path>/memory`, which is a symlink to that
directory; on a new machine run `scripts/link-claude-memory.sh` once (Windows: `scripts/link-claude-memory.ps1`, which makes a
directory junction). Memory
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
  touched; it runs everything by itself when `cmake/`, `third_party/` or an `engine/foundation` header changed, or when a
  module's or the root `CMakeLists.txt`, the phase map or the module map had a line edited or removed (a change that only adds lines,
  such as a new module or test, selects just its own phases; the `CMakeLists.txt` of a directory under `tests/`, `tools/` or `game/` never
  widens it). It says why before the build starts, and **a changed path that is in no line of `tests/module_map.txt` is an error that
  stops before building** (add the line in the day the directory appears: the ctest `test_phase_script_module_map_paths` fails on a
  source directory without one). Before the end-of-day review, also run the sanitizer flavors of the same selection
  (`--affected --flavor asan`, then `--flavor tsan`) when the day touched concurrency, lifetime or memory code.
- **A reduced run is reported as what it was**: the day file and `STATUS.md` quote the script's final line
  (`ran X of Y tests (phases ...)`) and never say "all tests pass" for a run that left tests out.
- **A phase's last day runs the whole suite in every flavor**: `scripts/test-phase.sh --all --flavor all`, plus
  `ctest -L docs` and the demos the phase ships, and records the line in the day file. A phase is not complete without
  it. `CURRENT_PHASE` changes only in the commit that starts the next phase.
- **Side phases (confirmed by the user, 2026-11-11; Phase 21 Day 283):** a phase the user runs beside the main line, on
  another machine (Phase 21, the macOS port), was listed in `SIDE_PHASES` and did not change `CURRENT_PHASE`; its line was removed on Phase 22 Day 313, when `CURRENT_PHASE` passed it (the closing runs it left are in `KNOWN-ISSUES.md`). Its tests carry its own label;
  `scripts/test-phase.sh --affected` adds every side phase on a machine that builds its tests (Phase 21 Day 286), and its days quote their own
  `ran X of Y` line. Its last day closes it with the full run like any phase, and removes it from `SIDE_PHASES`.
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

## Warnings are errors, on every platform (required)

Every target of ours (`engine/`, `game/`, `tools/`, `tests/`) compiles with warnings as errors: `-Wall -Wextra -Werror` on Clang/GCC, `/W4 /WX` on cl and clang-cl
(`ENGINE_WARNING_COMPILE_OPTIONS` in `cmake/EngineCompilerOptions.cmake`, added by the root `CMakeLists.txt` after `third_party/`, so the vendored libraries keep their
own flags). **Fix the code; do not silence the warning.** No `-Wno-...`, no `#pragma` that disables a warning, no `(void)` to hide a defect. The rule starts as an absolute;
if a real exception turns up, it is added here with its reason (and to the CMake file) in the same commit, not decided locally. A change must build clean in unity and
`-DENGINE_UNITY_BUILD=OFF` (unused-function and unused-field warnings differ between the two), and with both compilers: on Linux CMake picks Clang when `clang++` is installed and GCC otherwise, and GCC reports things Clang does not (`-Wformat-truncation`, `-Wsubobject-linkage`).

## Platform, compiler, processor and sanitizer are asked through `build_defs.h` (required)

Never test a compiler's or system's own predefined macro (`_WIN32`, `__linux__`, `__APPLE__`, `__clang__`, `__GNUC__`, `_MSC_VER`, `__x86_64__`, `__aarch64__`, `_M_X64`,
`__SANITIZE_ADDRESS__`, `__SANITIZE_THREAD__`, `__has_feature`, `__has_builtin`) in `engine/`, `game/`, `tools/` or `tests/`. Include `build_defs.h` (`engine/foundation/`) and write
`#if ENGINE_PLATFORM_WINDOWS`, `ENGINE_COMPILER_CLANG`/`_GCC`/`_MSVC`/`_CLANG_CL`, `ENGINE_ARCH_X64`/`_ARM64`, `ENGINE_SANITIZER_ADDRESS`/`_THREAD` (always `#if`, never `#ifdef`).
A new platform, compiler or processor changes that header only. Rule 6 of the `platform_neutral_check` gate fails on a raw macro; the exceptions are the header itself,
`tests/windows/discovery/` and generated code. See `docs/guide/foundation-types.md`.

## Windows is a supported platform (Phase 13, required)

Windows x64 builds and runs everything, with the Vulkan and Direct3D 12 backends (`docs/guide/windows.md`, `BUILD.md`).

- **Build and test** from a plain PowerShell or Git Bash (never a Developer Command Prompt; the toolchain files find MSVC, the SDK and LLVM):
  `cmake --preset windows-msvc` / `cmake --build --preset windows-msvc` (tree `build-msvc`, plain and asan) and `windows-clang-cl`
  (`build-clang-cl`, plain only); tests through `scripts/test-phase.sh` under Git Bash, as on Linux. A change must build clean with both compilers
  and in both unity modes there as well.
- **Flavors per platform:** Windows runs plain and asan (MSVC has no ThreadSanitizer); tsan runs on Linux only. A phase's closing
  `--all --flavor all` therefore runs on both machines.
- **Windows code lives in Windows directories:** Win32, the Windows SDK, D3D12 and DXGI headers only under `*/windows/` (and `engine/rhi/dx12/`),
  and `windows.h` only through `engine/foundation/os/windows/win32.h` (the `platform_neutral_check`, `module_deps_check` and `rhi_boundary_check`
  gates enforce it).
- **Three machines** (the Linux machine, the Windows PC and, from Phase 21, the Mac): a day is built and tested on the machine it is done on; changes to
  shared code (CMake, the phase map, the gates, the scripts, shared tests) are checked on the other machines no later than the phase's close, and
  `KNOWN-ISSUES.md` lists what is waiting for those runs.
- **The graphics backend is chosen at start** (`renderer.backend`, `--rhi`; `auto` is Direct3D 12 on Windows): there is no run-time backend
  switch (the user's decision, 2026-11-08).

## macOS is a supported platform (Phase 21, required)

macOS builds and runs everything, with the Cocoa window and the Metal backend (`docs/guide/macos.md`, `BUILD.md`); there is no Vulkan on macOS (MoltenVK is
third-party, Phase 21 decision 8).

- **Build and test** with Apple clang from the Command Line Tools (no Xcode): `cmake --preset macos-clang` / `cmake --build --preset macos-clang` (tree
  `build-macos`, x86-64), tests through `scripts/test-phase.sh` (the system's bash 3.2). Flavors are plain, asan and tsan (no LeakSanitizer). A change must
  build clean in both unity modes there as well, and a day that changes macOS code also builds `macos-clang-arm64` (`build-macos-arm64`, compile-only:
  nothing runs) in both unity modes. Format with MacPorts' `clang-format` 23.x, never the Command Line Tools' Apple one.
- **macOS code lives in macOS directories:** Apple's headers and `.mm` files only under `*/macos/` (and `engine/rhi/metal/`); Metal only in
  `engine/rhi/metal/` and `tools/metal_mint/` (the `platform_neutral_check`, `rhi_boundary_check` and `net_access_check` gates enforce it). Objective-C++
  uses ARC, and every backend entry point that calls AppKit or Metal wraps its body in `@autoreleasepool` (callers may have none; `metal_autorelease_pools`).
- **Metal runs validated in the tests** (`MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=assert MTL_SHADER_VALIDATION=1` in the ctests): a validation error
  stops the test. Window tests need the Mac's console unlocked.
- **Shaders:** the `.metallib` files are minted on the Mac (`tools/metal_mint`, an interim route pending the user's next Mac); a block written by the CPU
  must lay out the same in Metal as in std140 (`shader_mint` refuses a difference).
- **The graphics backend is chosen at start** (`auto` is Metal on macOS), as everywhere.

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

**The end-of-phase review is uncapped.** A phase's last day also runs `/code-review high --max-findings all` over the whole phase's diff (every day of
the phase, from the commit before its first day to the working tree), so that no finding is cut off by the default limit on the number reported. Verify,
fix or decline each finding as above, and record the count and the outcome under "Post-implementation code review" in the last day's file. The ordinary
end-of-day review keeps the default limit.

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

## Code style: clang-format decides (required)

`.clang-format` is the style: **Allman braces** (every `{` on its own line: functions, classes, enums, namespaces, control flow), **every `if`, `else`, `for`, `while`
and `do` body braced** (clang-format inserts missing braces), 4-space indent, **120 columns**, pointers bind to the type (`Socket* out`), no namespace indent, include
order untouched (it is part of the build: the unity enable/disable pairs). Do not format by hand and do not argue with the formatter in a review.

- **Format with `scripts/format.sh`** (everything tracked, or the files given; `--check` only reports). Files in `.clang-format-ignore` (vendored code, generated code,
  test fixtures with planted violations) are never touched.
- **Install the hook once per machine: `scripts/install-git-hooks.sh`** (Windows: from Git Bash). The pre-commit hook formats the staged C and C++ files and stages them again;
  it refuses a file that also has unstaged edits, and refuses to commit when clang-format (18 or newer, `BUILD.md`) is missing. `--no-verify` is not for routine use.
- **Comments are wrapped by clang-format at 120 columns, and doc-comment labels are list items** (`/// - Fails: never.`, `docs/DOC-CONVENTIONS.md`): that is the form its comment reflow leaves on a line of its own; a bare
  `Fails: ...` is a docgen error. A wrapped label or `///<` comment continues on the flush lines below it. Long string literals are not split.
- **A formatting-only change is its own commit** with no other edits, so review and `git blame` stay useful (list it in `.git-blame-ignore-revs`).
- **clang-tidy** (`.clang-tidy`, `scripts/tidy.sh`) runs on demand over a non-unity compile database; fix a finding or suppress it on its line with
  `// NOLINT(check): reason` (a reason is required). It is not in the hook.

## Always use the wrapped types, never `std` (required, Phase 16)

Code in `engine/`, `game/`, `tools/` and `tests/` includes no standard header, spells no `std::` name, calls no unqualified C library function (`strlen`, `memcpy`, `printf`, `qsort`, ...) and spells no raw `unsigned char` / `(std::)u?int(8|16|32|64)_t`.
Every facility has an engine wrapper in the module of its category (`PLAN.md` assumption #23); the wrappers are inline and zero-cost, so there is no reason to go around them:

| I need | Header | Use |
|---|---|---|
| traits, `Move`/`Forward`/`Swap`, `Limits`, `Ordering`, `InitializerList`, placement `new`, scalar math | `lang/*.h` | `IsSame<A,B>`, `Move(x)`, `Limits<T>::Max()`, `ConstructAt<T>(p, ...)`, `Sqrt`, `Floor`, `Abs`, `IsNaN` |
| atomics, mutexes, condition variables, threads | `sync/*.h` | `Atomic<T>` + `MemoryOrder`, `Mutex`, `LockGuard`, `ConditionVariable`, `Thread`, `ThisThread` |
| raw memory, C strings, formatting, number parsing, variable arguments | `cstr/*.h` | `MemCopy`, `MemSet`, `CStrLength`, `FormatTo`, `ParseDoubleExact`, `SortRaw`, `ENGINE_VA_START` |
| console, abort/exit, environment, errno, signals, clocks | `os/*.h` | `os::Print`, `os::PrintError`, `os::Abort`, `os::GetEnvironment`, `os::LastError`, `os::SetSignalHandler`, `os::MonotonicTimeNanos` |
| coroutines | `jobs/coroutine.h` | `CoroutineHandle`, `SuspendAlways` |
| strings, sequences, maps, owned pointers, views, sorting, fixed-width integers | Phase 10 types | `String`/`StringView`/`StringBuilder`, `Vector`, `Array`, `HashMap`, `UniquePtr`/`SharedPtr`, `Span`, `Sort`/`Min`/`Max`, `byte`, `u8`..`u128` |

**If a facility has no wrapper yet, add the wrapper to the module of its category first** (with `///` docs, a test and, for the rare oracle test, a line in the allow list), then use it; never include the standard header at the use site.
The gate `std_usage_check` (`tools/std_usage_check`, ctest `std_usage_check`) fails on every violation; its allow list `tools/std_usage_check/allowlist.txt` has one `path|headers|reason` line per exception (the wrapper modules with the headers each may include, `types.h`,
`tools/shader_mint/reflect_json.cpp` and its test for nlohmann/json, `tests/support/catch_engine_types.h` for Catch2, oracle tests that compare a wrapper with the standard function it forwards to, and the Windows-only files until their port), and a stale entry fails.
Names the compiler requires stay inside the wrapper modules (`std::initializer_list`, `std::strong_ordering`, `std::align_val_t`, `std::nothrow_t`, the coroutine names). Third-party code under `third_party/` is not touched. The POSIX and OS headers (`<time.h>`, `<signal.h>` types, `<unistd.h>`, X11, Wayland, Vulkan) are not the standard library, and their calls (`::time`, `::sigaction`, `::read`) are the operating system's, written with the `::` prefix; where `os/` has a wrapper (`os::SetSignalHandler`, `os::MonotonicTimeNanos`, `os::Write`) use it. The gate's list of banned C names (`std_usage_check/check.cmake`) is the C library's own functions, not POSIX.
See `docs/guide/foundation-cstr.md`, `foundation-sync.md`, `foundation-types.md` and `docs/guide/building-and-testing.md`.

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
  (`libX11`/`libxcb`, with `libXcursor`, `libXi` and `libXfixes` of the same family), Win32, Cocoa, and GPU driver entry points
  (Vulkan/DX12/Metal). The dividing line is "ships with the OS/distro
  itself" vs. "third-party convenience/abstraction library built on top of
  one" — see `PLAN.md`'s "Assumptions & scope notes" for the precise
  reasoning on each boundary.
- **Foundation layer still hand-rolls its own core.** Custom allocators, containers, and string handling instead of reaching for the C++ STL; the facilities that are solved problems (scalar
  math, concurrency primitives, C++20 coroutines, the C library's memory and string functions) are not re-implemented but are used **only through the engine's wrapper modules** (`lang`, `sync`, `cstr`, `os`,
  `jobs/coroutine.h`; section "Always use the wrapped types, never `std`" above, `PLAN.md` assumptions #2 and #23), so each has one place to port, replace or optimize. The vector/matrix/quaternion
  *types* built on top of scalar math are hand-rolled. Exceptions and RTTI remain disabled.
- **The standard library is not used outside the wrapper modules** (Phase 10 for containers and strings, Phase 16 for everything else): see the section "Always use the wrapped types, never `std`" above for the wrapper of each facility
  and the `std_usage_check` gate. Test helpers: `tests/support/test_text.h`, `tests/support/catch_engine_types.h`; see `docs/guide/building-and-testing.md` ("Writing a test") and `docs/guide/foundation-types.md`.
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
  defined in Phase 4. Vulkan (Phase 4) and Direct3D 12 (Phase 13) are the backends; Metal (Phase 21)
  follows behind the same interface.
- **Data-oriented ECS**, not an OOP scene graph.
- **The engine must stay buildable as a loadable library** (Phase 23, planned; `PLAN.md` assumption #27): the same sources build as static libraries (the default) or as one shared library with hidden symbols and a versioned flat C ABI at the boundary. Until Phase 23 exists, do not add global or static state that could not live once inside one library (a second copy per module would be a bug), do not let an owning pointer or an allocation cross a module boundary to be freed by another module, and do not make a module depend on its own static initialization order.
- **Linux first, Wayland primary / X11 fallback.** Every system is built
  and proven on Linux before porting to Windows/macOS/Android/iOS/consoles.
  Windows is Phase 13, right after the renderer core and the layering
  phases, and ports what exists by then (platform layer, RHI, renderer,
  shaders). **The macOS + Metal port is Phase 21** (postponed there on 2026-11-08,
  built on the Mac beside the main line, 2026-11-10 to 2026-11-13), just before Android/iOS (Phase 28, which reuses its Metal
  backend); consoles are Phase 29. How the later
  Linux-first phases (7-13) keep the Windows/macOS builds working is an
  open decision, see `PLAN.md` assumption #11. On Linux, Wayland is the primary display backend, X11 is
  a fallback backend for systems that haven't moved to Wayland — both
  built on their reference client libraries (`libwayland-client`,
  `libX11`/`libxcb`) rather than a hand-rolled wire-protocol client.
- **The RHI can be reset at run time (Phase 8).** Users must be able to change any rendering setting without restarting
  the app (except the graphics backend, which is chosen at start: there is no run-time Vulkan/DX12 switch, the user's decision of
  2026-11-08; the GPU can still be switched), so re-initializing the device, swapchain and every device-dependent resource is a supported operation, not
  a recovery hack. Anything that owns GPU resources must therefore be able to rebuild them (keep its CPU-side source
  or a reload path), and a lost device recovers through the same path. Do not add a GPU-owning type that cannot be
  recreated. The concrete rule (`docs/guide/renderer.md`, "Surviving a reset"): an owner of device objects **keeps its CPU-side
  source, registers with `renderer::DeviceResources` at its tier (a `ScopedRegistration` member), implements `Release`/`Recreate`,
  and never caches a handle past a frame** (hold the owner and read `Handle()` when recording). Materials bind textures by owner,
  not by raw handle. A test of such an owner resets the device through `tests/renderer/reset_rig.h` and compares the counts from
  `Device::Resources()` (`docs/guide/building-and-testing.md`, "Testing a reset"). Rendering settings change at run time through
  `Renderer::ApplySettings` and are classified by cost (`docs/guide/render-settings.md`).
- **All file and directory access goes through the file layer (Phase 9): `io::File`, `io::Path`, `io::Directory`** (`engine/foundation/io/`; guide
  `docs/guide/io.md`; design `docs/superpowers/specs/2026-11-06-file-layer-design.md`; `PLAN.md` assumption #21). `File` is an interface with synchronous and
  asynchronous operations (only the local file is implemented; the network-like test double proves it fits others); `Path` is a value type with no syscalls and
  a `PathStyle`, so Windows rules are tested on Linux; errors are values (`io::Status`). Do not call `fopen`, `open`, `opendir`, `stat`, `mkdir`, `rename`,
  `realpath`, `getcwd`, `std::ifstream` and the like, or paste path strings, in `engine/`, `game/`, `tools/` or `tests/`: the `file_access_check` gate
  (`tools/file_access_check`, ctest `file_access_check`) fails on them. Its allow list (`tools/file_access_check/allowlist.txt`, a reason per entry, a stale
  entry fails) holds the documented exceptions: the Linux backend itself, the descriptor primitives of `os/` (`os::Open`, `os::CreateFileForWriting`), code between
  `SIGNAL-SAFE` markers, `memfd` shared memory, console output (`os::Write`, stdout and stderr), tests that plant raw file-system shapes on purpose, and third-party
  code. A new platform implements `engine/foundation/io/BACKEND.md` (kept in step by `io_backend_lists`). Config files and the settings system (Phase 11) are built
  on this layer.
- **Anything a user can configure is a setting** (Phase 11): declared with `settings::Registry` (`engine/foundation/settings/`; guide `docs/guide/settings.md`;
  design `docs/superpowers/specs/2026-11-07-settings-design.md`; `PLAN.md` assumption #22) and read through `Setting<T>` or `Settings`. Do not add an ad hoc `getenv`, a
  `--flag` parse or a configuration file format of your own in `engine/` or `game/`: declare the setting with its `env` and `flag` names and the settings system reads
  them. The `config_access_check` gate (`tools/config_access_check`, ctest `config_access_check`) fails on them outside its allow list
  (`tools/config_access_check/allowlist.txt`, a reason per entry, a stale entry fails). Exceptions: variables read before any `Settings` can exist (window host, job
  scheduler, crash handler, Vulkan loader and validation switches), the death-test child protocol, the launch-only options of demos and benchmarks, third-party code and
  the tools' own command lines. A new platform store implements `engine/foundation/settings/BACKEND.md` (kept in step by `settings_backend_lists`).
- **Spatial queries go through one module** (Phase 18, `engine/spatial`; planned, not built yet): physics' broad phase, the renderer's culling, audio emitter lookup, gameplay proximity and ray queries use its structures and its query vocabulary,
  fed from entities by its sync system. Do not write a private grid, tree or "find nearby" scan in another module once it exists; if a need is missing, extend the module (`PLAN.md` assumption #24).
- **Entities are saved and loaded through one storage interface; SQL goes through one database interface** (Phase 19, `engine/storage` and `engine/database`; planned, not built yet; `PLAN.md` assumption #25): do not write a save format
  or a private SQL call in game or engine code. **SQLite** is a deliberate, individually justified third-party exception (same category as FreeType and LuaJIT), vendored under `third_party/sqlite/` and included only by its
  implementation file behind the database interface.
- **Network traffic goes through the networking layer** (Phase 20, `engine/net`; planned, not built yet; `PLAN.md` assumption #26): raw socket calls only in its per-OS backend directories. TCP and UDP first; QUIC (and TLS) is
  decided by a spike at the start of the phase, by the user, and adds a dependency whichever way it goes.
- **Every target platform is designed in from day one — desktop, mobile,
  and console alike — even though only Linux is implemented before
  Phase 13 (Windows).** Platform-layer and engine-facing interfaces must stay
  shaped so no later backend (Phases 13, 21, 28 and 29) ever forces a redesign of
  already-built engine layers — see `PLAN.md` assumption #11. Build order
  still follows the roadmap (Linux → Windows/macOS → Android/iOS →
  consoles); that's not license to design as if mobile/console don't
  exist until their phase starts.
- **Font rendering is backed by FreeType** (Phase 14), not a hand-rolled
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
  existing grapheme clustering. **libpng and zlib are vendored with it from Phase 27 Day 347** (the user's decision, 2026-11-10), used only by FreeType
  to decode the PNG strikes of `CBDT`/`sbix` colored-glyph fonts; no engine file includes their headers.
- **Gameplay scripting embeds real languages, not a custom bytecode VM —
  starting with Lua via LuaJIT** (Phase 26). Deliberate, individually
  justified exception to the third-party-dependency line, ships inside
  the runtime engine/game binary — see `PLAN.md` assumption #13. Designed
  with a pluggable language-backend seam so more languages can be added
  later without a redesign. Phase 31's Blueprint importer targets this
  (transpiling to Lua) instead of a custom VM.
- **Unreal Engine project import is a headline objective**, not a nice-to-
  have (Phases 30 and 31): loading a real, unmodified UE 5.3+ project's
  meshes/textures/materials/levels, and eventually Blueprint gameplay
  logic, directly into this engine. Deliberately sequenced *after*
  multiplatform support (Phases 13, 21, 28 and 29) rather than right after the asset
  pipeline (Phase 22) — see `PLAN.md` assumption #14. Targets uncooked editor-format
  projects, not shipped/cooked games. The offline import tool links a DLL
  built from Unreal Engine's own source to parse `.uasset`/`.umap`
  content via UE's real `UObject`/package-loading system, rather than a
  from-scratch reverse-engineered parser — scoped to the import tool
  only, never linked or shipped in the engine/game runtime (see
  `PLAN.md` assumption #6). **Open risk, unresolved:** Epic's UE EULA
  terms need review before Phase 30 work starts — flagged in `PLAN.md`,
  not yet resolved.
- **The GUI is the engine's own widget toolkit, not a third-party one**
  (Phase 15): a retained-mode widget set (menus, buttons, comboboxes,
  scrollbars, labels, text fields, ...) built on the RHI and the Phase 14
  text layer, including a **viewport widget** that hosts a rendered 3D
  world. No Qt/GTK/Dear ImGui-style dependency — that would breach the
  zero-third-party-convenience-dependencies rule. In-game UI, debug
  tooling and the editor all share it.
- **Shaders are authored in Slang and compiled offline, never at
  runtime** (`PLAN.md` assumption #18). Sources are `shaders/*.slang`,
  compiled by `slangc` (dev-time, optional) to SPIR-V and embedded as
  committed `*.spv.inc` files; a checkout without `slangc` builds from
  the committed outputs. Don't add GLSL or hand-written SPIR-V. DX12 and
  Metal output comes from the same Slang sources in Phases 13 and 21 (note
  the clip-space Y flip and Metal's `main` rename in `shaders/README.md`).
- **Debugging facilities are built on existing libraries** (Phase 7): libbacktrace (stack traces and symbols) and
  sentry-native 0.17.6 with its Breakpad backend and **no upload** (`none` transport, crash capture and minidumps only) - a
  deliberate, individually justified exception chosen by spike (`PLAN.md` assumption #20,
  `docs/superpowers/specs/2026-11-06-debugging-design.md`). Vendored as source under `third_party/` and used only through
  the facade in `engine/foundation/debug/`: only its two implementation files include the libraries' headers, enforced by a
  gate. The assert family, crash context, death-test helper and core-dump scripts stay ours. Do not hand-write what these
  libraries already do, and do not add a network path. **On Windows, symbols come from DbgHelp** (the system's own symbol
  library, reading the PDBs; libbacktrace is not built there: Phase 13 Day 154, `PLAN.md` assumption #20), and the minidump
  scripts are PowerShell (`scripts/*.ps1`).
- **Offline tools may use a vetted third-party library** where the job is
  solved and not engine-differentiating - today only nlohmann/json
  (vendored under `third_party/nlohmann_json/`) in `tools/shader_mint`,
  to read `slangc` reflection (`PLAN.md` assumption #19). Never linked
  into the engine, a game, the renderer or an engine test target (the tool's own tests link it); anything else
  needs its own entry in `PLAN.md`. That tool and its tests may use `std::string`/`std::vector` (the library
  requires them); the engine's runtime libraries still do not.
- **Terminology: we mint assets, we do not cook them.** Offline
  asset/shader processing tools are "mints"/"minters" (`tools/shader_mint`,
  the Phase 22 asset minter) and their outputs are "minted". "Cook" appears
  only when describing Unreal's pipeline. Minting must be deterministic:
  same inputs and tool version give byte-identical outputs, tested for
  every minter (see `PLAN.md`, guiding principles).
- **Engine core before editor.** The editor (Phase 32) is built last,
  using the engine's own renderer/ECS and Phase 15 UI system — not a
  separate GUI toolkit.

## Phases are whole numbers; renumber when the plan changes

**Never add a decimal or lettered phase** (no "8.5", no "Day 124a"). When a phase or day is inserted, renumber everything after it in the same commit: the plan files and directories
(`plans/phase-NN-*`, `day-NNN-*`), `PLAN.md` (roadmap, log, assumptions), `STATUS.md`, this file, the memory notes, the docs, and every code comment, test name and message that names
a phase or a day. There are no decimal phases anywhere (the documentation tooling, once `2.5`, is Phase 3), and the test-phase tooling only accepts whole numbers.

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
