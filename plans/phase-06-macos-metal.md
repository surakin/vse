# Phase 6 — macOS Port + Metal Backend

**Depends on:** Phase 4 (Renderer core); benefits from Phase 5 having
already worked through the "porting an already-working stack" process once.
Before it starts, every day of this phase needs its own file under
`plans/phase-06-macos-metal/`.
**Goal:** Phases 1-4 running on macOS, with Metal as the RHI backend.
**Definition of done:** the Phase 3 golden images and the Phase 4 scene
tests pass on macOS through the Metal RHI backend with the Metal API
validation layer silent; the job system and foundation tests pass on macOS.
iOS is Phase 14 and reuses this phase's Metal backend.
**Hardware:** the developer's 2019 MacBook Pro, an **Intel** Mac with an
Intel or AMD GPU (x86-64 macOS). That exercises the discrete-GPU path
(`Managed` storage, explicit CPU/GPU synchronization) and the Metal
validation layer. It does **not** exercise Apple Silicon behavior (unified
memory, tile-based rendering, memoryless attachments), so anything the
backend does for those is unverified until an Apple Silicon Mac or an iOS
device is available; record such items in `KNOWN-ISSUES.md`. Also confirm
the newest macOS and Xcode this machine can run before choosing the
Metal feature level.

## Key deliverables (to be expanded into day-by-day tasks when started)

- macOS platform layer: Cocoa/AppKit window+input (raw Objective-C
  runtime message calls, no CRT convenience wrappers beyond what's
  unavoidable to talk to Cocoa).
- Metal RHI backend implementing the Phase 3 RHI interface.

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 5 is complete.
- The iOS platform layer (UIKit, touch) and the LuaJIT interpreter-only build for iOS moved to Phase 14.
- Objective-C/Swift runtime interop from a C++-rooted, no-CRT codebase
  needs a concrete decision on calling convention/bridging approach —
  scope this with real Metal headers in hand, not speculatively.

## Shader toolchain (from PLAN.md assumption #18)

The backend needs MSL shaders. The toolchain that produces them from the
engine's shader source was to be settled before Phase 4; confirm that
decision here and that the mint emits MSL blobs for `ShaderDesc`
before this phase's day files are written.

- **From the Phase 3 portability review** (`docs/superpowers/specs/2026-11-04-rhi-portability-review.md`, section 5):
  split encoders around render passes, copies and dispatches; barriers become no-ops, `memoryBarrier` or an encoder split
  depending on resource tracking mode; threadgroup size comes from shader reflection at pipeline creation; `Managed`
  storage on discrete Macs maps `FlushMappedBuffer` to `didModifyRange`; `ClampToBorder` is unavailable on iOS (the
  backend rejects it with a log; that matters for Phase 14, not here); `nextDrawable()` is `Acquire`; the clip-space Y flip lives in the shader translation
  path.

## What Phase 4 gives this phase (Phase 4 Day 84)

From the renderer (see `docs/superpowers/specs/2026-11-04-renderer-portability-review.md`):

- `shaders_portability` compiles every shader to Metal Shading Language today. Known gaps to close here: `fill.comp` and `saxpy.comp`
  call `GetDimensions` on a buffer, which Metal cannot do (pass the element count); Metal renames `main` to `main_0`; slangc flattens
  descriptor sets to `[[buffer(n)]]` slots, so the backend reads slots from the compiled reflection; the per-object index comes from
  `[[instance_id]]` plus `[[base_instance]]`.
- Graph barriers become no-ops on tracked resources; the golden images are the oracle.
- The deferred G-buffer is memory-bound on Apple-silicon tilers; merging the geometry and lighting passes into one render pass with
  memoryless attachments is a Phase 14 graph feature, not needed on macOS GPUs here.
