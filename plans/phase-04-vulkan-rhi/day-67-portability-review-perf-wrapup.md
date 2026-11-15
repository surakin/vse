# Phase 4 / Day 67 — Portability Review, Performance Baseline & Phase Wrap-up

**Phase:** [Phase 4 — Vulkan RHI](../phase-04-vulkan-rhi.md)
**Depends on:** Day 66.

Close the phase: prove the interface is not Vulkan in disguise, record a baseline, and finish the docs.

## Tasks

- [x] Portability review document `docs/superpowers/specs/2026-11-04-rhi-portability-review.md`: walk every RHI type and call against Direct3D 12 (resources/heaps, root signature, descriptor tables, resource states, command lists/allocators, fences, swapchain flip model) and Metal (argument buffers, encoders, heaps, residency, drawables) — a table of "RHI concept → Vulkan → D3D12 → Metal", with every mismatch listed and one of: fits, needs an RHI change now, or documented backend-side shim. Make any cheap interface changes found **now** (with tests), file the rest in the Phase 6/28 plan files' notes.
- [x] Performance baseline (recorded in the day file, not asserted in CI): draw-call throughput for N cubes (1k/10k) with instancing-free draws, `Barrier` call cost, upload bandwidth through the staging ring (MB/s for 1 MiB/64 MiB uploads), command-list recording cost per draw, pipeline creation time with/without the cache; note GPU, driver and settings. Identify hotspots found (e.g. device mutex, descriptor writes) as Phase 5 notes — no optimization today unless a defect.
- [x] Documentation completion: `docs/guide/rhi.md` finished (overview, concepts, usage, rules & contracts, common mistakes, performance notes, platform notes, see-also) with status `draft`; `docs/guide/rhi-shaders.md` reviewed; `docs/tutorials/hello-rhi.md` finished (goal → triangle → cube → lit cube) with tagged samples; reference regenerated; lint baseline zero; `ctest -L docs` green.
- [x] Update `plans/phase-05-renderer-core.md` notes with the facts Phase 4 produced (including the "Deferred from Phase 4" table, whose D3D12/Metal impact notes this review fills in; and the render-graph decision input the Phase 5 file asks for: how much state tracking the RHI does, barrier cost, descriptor model), and `PLAN.md`/`STATUS.md`: Phase 4 complete; Phase 5 still needs its day files before it starts.
- [x] Final full run: plain, ASan, TSan, docs, GPU tests with validation + sync validation; record the results in the day file.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the guide page reaches `draft` status, the tutorial is complete and compiling, and the reference is regenerated with zero lint findings.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** the portability review is written with its cheap fixes applied, the baseline numbers are recorded, all documentation checks and test flavors pass, and Phase 4 is marked complete in `STATUS.md`/`PLAN.md`.

## Result

**Phase 4 is complete.**

- **Portability review** (`docs/superpowers/specs/2026-11-04-rhi-portability-review.md`): every RHI concept mapped to Vulkan, Direct3D 12 and Metal with a verdict (fits / shim / change). Result: the interface is not Vulkan in disguise; one interface change was needed and made now, the rest are backend-side shims filed in the Phase 6 and 25 plan notes. **The change:** D3D12's buffer-to-texture copy needs a 512-byte offset and a 256-byte row pitch; `BufferTextureCopy` allowed any texel-aligned pitch with 0 meaning "tightly packed", so Vulkan-tested code would have broken on D3D12. Now `DeviceCaps::buffer_texture_copy_offset_alignment` / `row_pitch_alignment` (default 512 / 256), enforced by the null device and the Vulkan backend (which reports the strict values on purpose); tests: the strict-default test, the Vulkan caps check, the null texel tests on a unit-alignment device, the Vulkan copy test with a 256-byte row. Two decisions recorded rather than changed: `ShaderDesc` stays SPIR-V until the shader-toolchain decision (two outcomes laid out in section 3), and any shader translator must flip clip-space Y for D3D12/Metal.
- **Performance baseline** (`game/rhi_bench`, run by hand, never in CI). NVIDIA GeForce RTX 3060, driver 615.78.08, AMD Ryzen 9 7950X3D, Linux 7.2, **Release build, validation off**, 512x512 offscreen target, two runs:

| Measurement | Result |
|---|---|
| Record 1000 cubes (uniform-ring slice + transient bind group + pipeline + draw each) | 0.50-0.52 ms/frame, ~0.5 us per draw; wall 0.52-0.54 ms |
| Record 10000 cubes | 6.2-6.4 ms/frame, ~0.63 us per draw; wall 6.5-6.7 ms (GPU keeps up) |
| `Submit` of the whole frame | 2-13 us |
| `Barrier` recording | buffer 0.15-0.17 us, texture 0.17-0.21 us |
| Upload through the staging ring | 1 MiB x 256: 15.6-18.1 GiB/s (0.06 ms each); 64 MiB x 8: 13.7-18.6 GiB/s (3.4-4.7 ms each) |
| Cube pipeline creation (shaders + pipeline + buffers) | cache off 5.6-5.8 ms, cold cache 4.9-5.0 ms, warm cache 3.6 ms |
| Debug build, validation off (for reference) | 1.5 us per draw, 0.23 us per barrier, 4.5-4.9 ms pipeline |

  (An early run on the Debug build with validation on gave 7.6 us per draw: the layer costs more than the engine, which is why the baseline turns it off.) **Hotspots** from `perf record` on the 10000-draw case: the NVIDIA driver ~12%, `pthread_mutex_lock` ~11% (the device resource mutex, from `CreateBindGroup` and the handle lookups behind `SetPipeline`/`SetVertexBuffer`), `CreateBindGroup` ~8%, `feclearexcept` ~6% (from libm, inside the driver path; not investigated), the command list's own methods a few percent each. No defect found, nothing optimized; the per-draw transient bind group is the first thing Phase 5's material system should avoid, recorded in `plans/phase-05-renderer-core.md`.
- **Documentation:** `docs/guide/rhi.md` is `draft` (seed scaffolding removed; Common mistakes, Platform notes, Performance notes and See also written; no `_TODO_` left); `docs/guide/rhi-shaders.md` reviewed (corrected its claim that the RHI is unaffected by the shader-toolchain choice); `docs/tutorials/hello-rhi.md` now runs goal -> triangle -> cube (new section with a tagged, running depth + indexed-draw sample) -> texture -> lit; reference regenerated (1691/1691, lint baseline zero); `ctest -L docs` green; the review is linked from `docs/README.md`.
- **Plans:** `plans/phase-05-renderer-core.md` gained "Facts Phase 4 produced" (state tracking, descriptor model, performance, threading, conventions, copy alignment, shader format) and the D3D12/Metal impact of each deferred feature; Phase 6 and 25 notes carry the review's shims; `plans/phase-04-vulkan-rhi.md`, `PLAN.md` and `STATUS.md` mark Phase 4 complete. Phase 5 still needs its day files before it can start.
- **Final run (2026-11-04):** `ENGINE_RHI_REQUIRE_GPU=1 ctest -j6`: **1280/1280** (plain, ASan 288, TSan 221, docgen 336, jobs 77, boundary 6, demo 6, docs 4 with 1691/1691 documented symbols); GPU tests run with validation + synchronization validation through `GpuFixture` and report nothing; `rhi_demo --validation` 300 frames on Wayland and X11: zero messages; `ENGINE_VULKAN_LIBRARY=/nonexistent ctest -L rhi`: all pass or skip in ~1 s.

## Post-implementation code review

`/code-review high` returned 7 findings.

Fixed: (1) The Vulkan backend reported alignment 1, so real runs never exercised the new contract (a Vulkan-only caller would ship code that breaks on D3D12): it now reports the strict 512/256, with a caps test, and the Vulkan copy test uses a 256-byte row. (2) The `Aborts:` text of `CopyBufferToTexture` omitted the alignment failure: added. (3) A zero alignment in caller-supplied `NullDeviceOptions::caps` divided by zero: zero now means "none"; caps documented as at least 1. (4) The documented meaning of `buffer_row_pitch == 0` ("tightly packed") was a trap under the new rule: the doc now says 0 is legal only when the tight row already meets the alignment. (5) `rhi_bench` ignored `UploadBuffer` failures, which would have recorded a bogus bandwidth: it now checks `ok` and prints no figure on failure; also fixed its early-return leaks and an unchecked `--draws` range. (6) The day file and `STATUS.md` were not yet updated: done in this commit.

Declined: none.
