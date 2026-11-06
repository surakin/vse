# Phase 7 / Day 114 — Portability, Docs, Closing Full Run & Wrap-up

**Phase:** [Phase 7 — RHI Reset](../phase-07-rhi-reset.md)
**Depends on:** Days 103-113.

Make the lifecycle something DX12 and Metal can implement without redesign, finish the documentation, run everything, and
close the phase.

## Tasks

- [ ] **Backend conformance list** for the lifecycle (as Phase 6 did for debugging): `engine/rhi/BACKEND-LIFECYCLE.md` states what a backend must provide (`Quiesce`, `ResourceCounts`, handle epochs, `Reconfigure`, adapter enumeration with a stable id, `EnumerateAdapters`, surface re-creation, tolerant destruction after loss, pipeline-cache identity per adapter), and a test (`rhi_lifecycle_lists`, like `debug_backend_lists`) keeps the list in step with the `Device` and `Swapchain` interfaces; the null backend passes the whole conformance suite, and the suite is written so a new backend runs it by adding one factory.
- [ ] Portability review: DXGI (factory re-creation, LUID adapter id, flip-model swapchain `ResizeBuffers`, `DXGI_ERROR_DEVICE_REMOVED` and `GetDeviceRemovedReason`, HDR10 and scRGB color spaces, fullscreen as borderless), Metal (`MTLCopyAllDevices`, `CAMetalLayer` drawable size and `colorspace`, no device-lost concept so recovery is for the `Null` and external-GPU removal only, EDR for HDR); append them to `docs/superpowers/specs/2026-11-04-rhi-portability-review.md` and to the Phase 8 and 9 plan files, with the new tasks those phases inherit. Cross-target syntax checks (clang `-fsyntax-only` for the new headers on the Windows and macOS targets, like Phase 6) pass.
- [ ] Decide and record the stretch goal: **backend switch at run time.** Null to Vulkan is covered by tests; Vulkan to DX12/Metal waits for those backends — file it in `KNOWN-ISSUES.md` (deferred, needed by Phase 8) and in the Phase 8 plan.
- [ ] Guide pass: read `docs/guide/render-settings.md`, `rhi.md` ("Lifecycle and reset", "Presentation", "Choosing a GPU", "Device loss"), `renderer.md` ("Surviving a reset", "Changing settings at run time") end to end as a new developer: a checklist "Making my type survive a reset" at the top of the renderer page; every tagged sample compiles and runs; `docs/guide/README.md` lists new pages; `docgen lint` at zero and the reference regenerated and committed.
- [ ] Update `CLAUDE.md`: the hard constraint "The RHI can be reset at run time" gets the concrete rule (register with `DeviceResources`, keep the CPU source, never cache a handle past a frame) and a pointer to the guide; PLAN.md assumption(s) that mention reset updated; `STATUS.md` build/test commands (GPU reset tests, the demo options).
- [ ] Readiness re-check: the boundary gate (no Vulkan names outside `engine/rhi/vulkan`), `platform_neutral_check`, signal-safety gate, no-upload gates still pass; the Phase 6 crash-state providers show epoch, reset count and recovery state; a Phase 6 `--crash-test` run during a reset produces a report (manual or scripted).
- [ ] Known issues sweep: every deferral filed (multi-adapter test skipped on one-GPU machines, HDR not visually verified without an HDR display, fullscreen on compositors that refuse, the DXIL/Metal paths, eager/lazy default if undecided); remove entries this phase fixed.
- [ ] **Closing full run (CLAUDE.md, required for a phase's last day):** `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, and both demos for 1000 frames on Wayland and X11 with validation on and silent, plus the scripted reset run (100 resets) on each; record the final `ran X of Y tests (phases ...)` line in this file; nothing left out.
- [ ] **End-of-phase review:** `/code-review high` over the whole phase's diff (Days 103-114 as committed), fix or decline each finding with a reason, record it under "Post-implementation code review".
- [ ] Update `plans/phase-07-rhi-reset.md` (status complete, days marked done), `PLAN.md` (roadmap row, progress-log entry), `STATUS.md`; `CURRENT_PHASE` stays 7 until Phase 8's day files exist and it starts (policy); commit.

**Done:** the lifecycle conformance list and its test pass, the Phase 8 and 9 plans carry the portability notes, guides and reference are current, the closing line `ran X of Y tests (phases: all; flavors: all)` is recorded with every test passing, both demos are clean on both display backends, every deferral is filed, and Phase 7 is marked complete.
