# Phase 16 — Standard-Library Wrappers

**Status: complete (2026-11-10) on Linux and on the Windows PC (Day 219 and its repeat of the closing run).** `CURRENT_PHASE` changes to 16 in the commit that completes Day 207's first task. Days 207-221. Inserted after Phase 15 (UI system) and before 3D text (now Phase 26); Phases 16-30 of the earlier plan are renumbered 17-30.

**Depends on:** Phase 10 (the type vocabulary and container gate, which this phase extends), Phase 12 (module layering: wrappers must respect `ARCHITECTURE.md`), Phase 13 (the Windows files, ported on the Windows PC), Phase 15 (the code to port is complete).
**Goal:** every use of the C++ standard library — and of the C library through `std::` — sits inside a wrapper module of its own category (`lang`, `sync`, `mem`, `cstr`, `os`, `jobs`, existing containers/strings). No other file includes a standard header or spells `std::`; a gate enforces it, tests included. Design and audit: `docs/superpowers/specs/2026-11-09-std-wrappers-design.md`.
**Definition of done:** the wrapper modules exist with tests (against the standard library as oracle), docs and guide pages; `engine/`, `game/`, `tools/` and `tests/` contain no standard include or `std::` outside the gate's documented list (the wrapper modules themselves, `third_party/`, the Catch2 and nlohmann/json boundaries); the job-system and log benchmarks show no regression beyond noise; a gate `std_usage_check` replaces `std_container_check`, with per-module header lists; `CLAUDE.md` and `PLAN.md` assumption #2 state the new rule; `CLAUDE.md` carries a permanent standing policy to always use the wrapped types instead of `std` (Day 221, the phase's last act); the whole suite passes in every flavor on Linux and on the Windows PC.

## Days

207. [Audit, Design Confirmation & Phase Setup](phase-16-std-wrappers/day-207-audit-and-setup.md)
208. [`lang`: Traits, Move, Limits, Ordering, `new` Helpers](phase-16-std-wrappers/day-208-lang-module.md)
209. [`sync`: Atomics, Mutexes, Threads](phase-16-std-wrappers/day-209-sync-module.md)
210. [Port Concurrency Uses in `engine/`, `game/`, `tools/`](phase-16-std-wrappers/day-210-port-concurrency.md)
211. [`mem` Operations and the `cstr` Module](phase-16-std-wrappers/day-211-mem-ops-and-cstr.md)
212. [Port `engine/foundation` to `mem`, `cstr`, `lang`](phase-16-std-wrappers/day-212-port-foundation.md)
213. [Port the Rest of `engine/` (platform, rhi, renderer, text, ui)](phase-16-std-wrappers/day-213-port-engine.md)
214. [Scalar Math, Time, Process, Signals, Console](phase-16-std-wrappers/day-214-math-time-process.md)
215. [Coroutines in `jobs`](phase-16-std-wrappers/day-215-coroutines.md)
216. [Port `game/` and `tools/`](phase-16-std-wrappers/day-216-port-game-and-tools.md)
217. [Port the Tests I: foundation, debug, jobs, io, settings, types](phase-16-std-wrappers/day-217-port-tests-1.md)
218. [Port the Tests II: platform, rhi, renderer, text, ui, docgen, shader_mint](phase-16-std-wrappers/day-218-port-tests-2.md)
219. [The Windows PC: Windows-Only Files and Both Compilers](phase-16-std-wrappers/day-219-windows.md)
220. [The `std_usage_check` Gate and the Rule](phase-16-std-wrappers/day-220-gate.md)
221. [Docs, Closing Full Run & Wrap-up](phase-16-std-wrappers/day-221-docs-and-closing-run.md)

Order reasoning: the audit (207) fixes the lists before any code. Wrapper modules come before their ports (208, 209, 211, 214, 215) so a port is mechanical; concurrency goes first (209-210) because it is the hot path and the benchmarks must be taken before and after. The foundation port (212) precedes the rest of `engine/` (213) because everything includes it. Tests go last (217-218) so they exercise the final wrappers. The Windows PC day (219) ports the files Linux cannot build; the gate (220) can then start at zero violations; 221 closes the phase.

## Design decisions made while planning (2026-11-09; Day 207 confirms or amends them in the spec)

1. **Several modules include the standard library, each its own category** (decided with the user); a gate lists the headers per module. Not one single module.
2. **Scope is everything, tests included** (decided with the user), except the documented Catch2 and nlohmann/json boundaries and `third_party/`.
3. **Hard parts are wrapped with inline zero-cost types** (`Atomic<T>`, `Mutex`, `LockGuard`, `Thread`, `CoroutineHandle`, `Move`/`Forward`/`IsSame`), checked by benchmarks. Compiler-mandated names (`std::coroutine_traits`, `std::initializer_list`, `std::strong_ordering`, `std::align_val_t`, `std::nothrow_t`) stay inside the wrapper modules.
4. **Every wrapper is additive**: the port of a category can be done file by file and the tree builds after each day; the Windows-only files are ported last, on the Windows PC.
5. **The C library is included** (`std::strlen`, `std::memcpy`, `std::snprintf`, `std::getenv`, ...), because the goal is "one place per category", and the phase reverses the permissive part of `CLAUDE.md` and assumption #2 ("concurrency primitives, `<cmath>`, the C library through `std::` are used directly").

## Notes / open questions

- Where scalar math lives (layer 0 `lang` vs layer 4 `math`) depends on which lower layers use it (Day 214 measures; Day 207 records the first answer).
- Whether `Duration`/`TimePoint` are worth having beyond the existing `os::` clocks (Day 214 decides from the 65 `std::chrono` uses).
- `printf`-style formatting stays variadic (`FormatTo`); a type-safe formatter is out of scope.
