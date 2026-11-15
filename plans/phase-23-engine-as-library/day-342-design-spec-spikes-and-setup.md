# Phase 23 / Day 342 — Design Spec, Spikes & Phase Setup

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Phase 22 closed (Day 341), Phases 2, 7, 11, 12, 13, 21.

Settles every open question of the phase file with the planner's defaults and three spikes, writes the spec, and starts the phase.

## Tasks

- [ ] Write `docs/superpowers/specs/2026-11-16-engine-library-design.md`: the **decisions** with reasons: one aggregate shared library for distribution and per-module shared targets as a development option (the recommendation of the phase file, confirmed or changed); the C API is hand-written with generated version tables; the exported entry point is one function returning a versioned function table (`engine_get_api(version, size)`); symbol versioning on Linux (a version script) or not; what iOS and consoles mean (static only, the C API headers still compile).
- [ ] **Spike 1, a shared foundation:** build `foundation`, `jobs` and `settings` as one `.so` with `-fvisibility=hidden` and a tiny C host that `dlopen`s it; record what breaks (static initializers, thread-locals, `--wrap` use, the crash handler's signal handlers, `RTLD_LOCAL` against `RTLD_GLOBAL`), what the size and the load time are.
- [ ] **Spike 2, one of everything:** two small shared libraries, both linking the static foundation, and a host loading both: show which singletons are duplicated (log channels, the settings registry, the job scheduler's thread-locals, the allocator statistics, the crash handler); then repeat with the foundation inside the one `engine` library and show they are one. Record the list of singletons found by `nm`/grep over `engine/` (function-local statics, namespace-scope objects, `thread_local`).
- [ ] **Spike 3, sanitizers and shared libraries:** AddressSanitizer with a shared engine library (`-shared-libasan`, interposition, leak reports at `dlclose`) and ThreadSanitizer with `dlopen`; record the flags and the limits (the Phase 22 libpng `longjmp` finding for tsan is one such).
- [ ] Decide in the spec, with the planner's default: the ABI versioning rule (major breaks, minor adds, a function table whose size says what exists), the error-code mapping of `asset::Status`/`ErrorCode`, string and buffer ownership rules, the callback thread rules, the handle format (index and generation), and the list of what `engine_c.h` v1 covers and what it leaves to later phases.
- [ ] Decide the embedding questions: the foreign-surface descriptor (versioned, size-prefixed), what `engine_caps` reports, the readback path first and shared textures second, the input forwarding (the host owns input and the clipboard, 2026-11-13), and ask the user to confirm the assumption that the host's toolkit exposes the raw native handle.
- [ ] Add the option `ENGINE_BUILD_SHARED` (default OFF) to the root `CMakeLists.txt` as a no-op that prints a message, with `BUILD.md` and `docs/guide/building-and-testing.md` text; the day's tests only check that the option exists and defaults off.
- [ ] Amend PLAN.md assumption #27 and the phase file with what the spec decided; set `CURRENT_PHASE` to 23 in this commit.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the spec is written with the decisions, three spike records and the list of singletons; the option exists; `CURRENT_PHASE` is 23.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
