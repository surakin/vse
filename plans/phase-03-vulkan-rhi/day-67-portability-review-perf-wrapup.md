# Phase 3 / Day 67 — Portability Review, Performance Baseline & Phase Wrap-up

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 66.

Close the phase: prove the interface is not Vulkan in disguise, record a baseline, and finish the docs.

## Tasks

- [ ] Portability review document `docs/superpowers/specs/2026-11-05-rhi-portability-review.md`: walk every RHI type and call against Direct3D 12 (resources/heaps, root signature, descriptor tables, resource states, command lists/allocators, fences, swapchain flip model) and Metal (argument buffers, encoders, heaps, residency, drawables) — a table of "RHI concept → Vulkan → D3D12 → Metal", with every mismatch listed and one of: fits, needs an RHI change now, or documented backend-side shim. Make any cheap interface changes found **now** (with tests), file the rest in the Phase 12/13 plan files' notes.
- [ ] Performance baseline (recorded in the day file, not asserted in CI): draw-call throughput for N cubes (1k/10k) with instancing-free draws, `Barrier` call cost, upload bandwidth through the staging ring (MB/s for 1 MiB/64 MiB uploads), command-list recording cost per draw, pipeline creation time with/without the cache; note GPU, driver and settings. Identify hotspots found (e.g. device mutex, descriptor writes) as Phase 4 notes — no optimization today unless a defect.
- [ ] Documentation completion: `docs/guide/rhi.md` finished (overview, concepts, usage, rules & contracts, common mistakes, performance notes, platform notes, see-also) with status `draft`; `docs/guide/rhi-shaders.md` reviewed; `docs/tutorials/hello-rhi.md` finished (goal → triangle → cube → lit cube) with tagged samples; reference regenerated; lint baseline zero; `ctest -L docs` green.
- [ ] Update `plans/phase-04-renderer-core.md` notes with the facts Phase 3 produced (the render-graph decision input the Phase 4 file asks for: how much state tracking the RHI does, barrier cost, descriptor model), and `PLAN.md`/`STATUS.md`: Phase 3 complete; Phase 4 still needs its day files before it starts.
- [ ] Final full run: plain, ASan, TSan, docs, GPU tests with validation + sync validation; record the results in the day file.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the guide page reaches `draft` status, the tutorial is complete and compiling, and the reference is regenerated with zero lint findings.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** the portability review is written with its cheap fixes applied, the baseline numbers are recorded, all documentation checks and test flavors pass, and Phase 3 is marked complete in `STATUS.md`/`PLAN.md`.
