# Phase 14 — Console Ports (Xbox, PS5)

**Depends on:** Phase 11 (Windows + DX12, closest analog to Xbox) and
Phase 3 (Vulkan RHI, closest analog to PS5's GNM/GNMX-adjacent needs —
exact backend TBD, see below).
**Goal:** the engine running on Xbox and PlayStation 5.
**Definition of done:** gated on external access — see below.

## Blocking constraint

Console development requires signed NDAs and vendor dev kits from
Microsoft (Xbox) and Sony (PlayStation) respectively. **This phase cannot
be scoped in real detail until that access exists.** Nothing here should
be treated as actionable until then.

## Key deliverables (high-level placeholder, real scoping needs vendor docs)

- Xbox: platform layer + RHI backend (likely DX12-family, reusing much of
  Phase 11's DX12 backend).
- PS5: platform layer + RHI backend targeting Sony's proprietary graphics
  API (not Vulkan/DX12/Metal — a fourth RHI backend).

## Notes / open questions

- Do not expand this phase further until dev kit/NDA access is secured.
