# Phase 3 — Renderer Core

**Depends on:** Phase 2 (Vulkan RHI).
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
  replaced by ECS in Phase 4).

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 2 is complete.
- Whether to build the render graph generically now (multi-backend-ready)
  or Vulkan-specific first and generalize later — decide against actual
  Phase 2 output, not in the abstract.
