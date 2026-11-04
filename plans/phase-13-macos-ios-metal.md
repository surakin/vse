# Phase 13 — macOS/iOS Port + Metal Backend

**Depends on:** Phases 0-11 (full stack on Linux); benefits from Phase 12
having already worked through the "porting an already-working stack"
process once.
**Goal:** everything built so far running on macOS and iOS, with Metal as
the RHI backend.
**Definition of done:** Phases 1-11's functionality running through the
Metal RHI backend on macOS (and iOS for the platform layer/input
differences touch/mobile bring).

## Key deliverables (to be expanded into day-by-day tasks when started)

- macOS platform layer: Cocoa/AppKit window+input (raw Objective-C
  runtime message calls, no CRT convenience wrappers beyond what's
  unavoidable to talk to Cocoa).
- iOS platform layer: UIKit + touch input.
- Metal RHI backend implementing the Phase 3 RHI interface.
- iOS build config for Phase 11's LuaJIT backend: JIT mode is disallowed
  on iOS App Store builds, so this is where the interpreter-only fallback
  (root `PLAN.md` assumption #13) actually gets wired up.

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 12 is complete.
- Objective-C/Swift runtime interop from a C++-rooted, no-CRT codebase
  needs a concrete decision on calling convention/bridging approach —
  scope this with real Metal headers in hand, not speculatively.

## Shader toolchain (from PLAN.md assumption #18)

The backend needs MSL shaders. The toolchain that produces them from the
engine's shader source was to be settled before Phase 4; confirm that
decision here and that the cooker emits MSL blobs for `ShaderDesc`
before this phase's day files are written.

- **From the Phase 3 portability review** (`docs/superpowers/specs/2026-11-04-rhi-portability-review.md`, section 5):
  split encoders around render passes, copies and dispatches; barriers become no-ops, `memoryBarrier` or an encoder split
  depending on resource tracking mode; threadgroup size comes from shader reflection at pipeline creation; `Managed`
  storage on discrete Macs maps `FlushMappedBuffer` to `didModifyRange`; `ClampToBorder` is unavailable on iOS (the
  backend rejects it with a log); `nextDrawable()` is `Acquire`; the clip-space Y flip lives in the shader translation
  path.
