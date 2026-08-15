# Phase 12 — macOS/iOS Port + Metal Backend

**Depends on:** Phases 0-10 (full stack on Linux); benefits from Phase 11
having already worked through the "porting an already-working stack"
process once.
**Goal:** everything built so far running on macOS and iOS, with Metal as
the RHI backend.
**Definition of done:** Phases 1-10's functionality running through the
Metal RHI backend on macOS (and iOS for the platform layer/input
differences touch/mobile bring).

## Key deliverables (to be expanded into day-by-day tasks when started)

- macOS platform layer: Cocoa/AppKit window+input (raw Objective-C
  runtime message calls, no CRT convenience wrappers beyond what's
  unavoidable to talk to Cocoa).
- iOS platform layer: UIKit + touch input.
- Metal RHI backend implementing the Phase 3 RHI interface.
- iOS build config for Phase 10's LuaJIT backend: JIT mode is disallowed
  on iOS App Store builds, so this is where the interpreter-only fallback
  (root `PLAN.md` assumption #13) actually gets wired up.

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 11 is complete.
- Objective-C/Swift runtime interop from a C++-rooted, no-CRT codebase
  needs a concrete decision on calling convention/bridging approach —
  scope this with real Metal headers in hand, not speculatively.
