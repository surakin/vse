# Phase 4 — Renderer Core

**Depends on:** Phase 3 (Vulkan RHI).
**Goal:** a real renderer built on the RHI — multiple objects, materials,
lighting, depth — not just a single hard-coded draw call.
**Definition of done:** a render graph/pass system draws multiple objects
with basic materials and lighting, with correct depth buffering.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Render graph / frame graph: pass declaration, resource dependency
  tracking, automatic barrier/transition insertion.
- Depth pre-pass or forward depth testing.
- A minimal material system (shader + parameters + textures) sitting on
  top of the RHI's pipeline objects.
- Basic lighting model (at least one of: forward-lit directional + point
  lights) sufficient to prove the pipeline, not a final lighting model.
- Camera/view/projection handling decoupled from any one scene format
  (ECS doesn't exist yet — this phase uses a minimal hard-coded scene list,
  replaced by ECS in Phase 7).

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 3 is complete.
- Whether to build the render graph generically now (multi-backend-ready)
  or Vulkan-specific first and generalize later — decide against actual
  Phase 3 output, not in the abstract.

## Deferred from Phase 3 (decide here, before the day files are written)

Phase 3 deliberately left these RHI features out (see
`plans/phase-03-vulkan-rhi.md`). Each needs an explicit in/out decision
when this phase's days are written; if "in", the RHI interface changes
and the Phase 3 portability review (Day 67) should be consulted for the
D3D12/Metal impact.

| Feature | Decide when | Already reserved in the Phase 3 RHI |
|---|---|---|
| **MSAA** (sample counts, resolve attachments, pipeline sample count) | With the forward-vs-deferred choice; MSAA is awkward with deferred and may be replaced by post-process AA | `samples` field in `TextureDesc` and the pipeline description, fixed at 1 |
| **Bindless / descriptor indexing** | With the material system design (per-material bind groups vs a global texture table) | `supports_bindless` flag in `DeviceCaps`, always false |
| **Indirect draws** (`DrawIndirect`, count buffers) | Only if GPU-driven culling is wanted | `indirect` buffer-usage flag; no draw command |
| **Async compute / dedicated transfer queue overlap** | Only if profiling shows a need | Compute runs on the graphics queue; uploads use the transfer queue where one exists |
