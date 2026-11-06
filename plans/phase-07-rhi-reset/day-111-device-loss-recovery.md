# Phase 7 / Day 111 — Device-Loss Recovery

**Phase:** [Phase 7 — RHI Reset](../phase-07-rhi-reset.md)
**Depends on:** Day 110.

`FrameStatus::DeviceLost` is terminal today ("the renderer stays destroyable but cannot render again"). After this day it
triggers the same reset with the saved settings.

## Tasks

- [ ] Recovery policy: on `DeviceLost` (or `Lost` from the swapchain that the surface re-creation of Day 107 cannot fix) the renderer runs a `DeviceReset` with the current settings; options in `RenderSettings`-adjacent `RecoveryPolicy`: `max_attempts` (default 3), delay between attempts (0 on the first, doubling, capped), `fallback_adapter` (try another adapter after the saved one fails; default on), `give_up` behavior (stay `DeviceLost`). Attempts and delays are counted in frames, not wall-clock sleeps, so tests are deterministic.
- [ ] Reporting: each attempt logs on `LogRenderer` (reason, attempt number, adapter), calls the Day 110 callbacks with a `Recovery` flag, bumps `RendererStats` (`recoveries`, `recovery_failures`), and the first loss keeps its Phase 6 ensure-style trace (once per site); crash-state shows "recovering (attempt N)".
- [ ] `RenderFrame` returns `DeviceLost` for the frame that discovered the loss and `Skipped`-like (decided in Day 110) for the frames spent recovering, then `Ok`; the frame counters still add up (`frames_lost` etc., existing invariant tested).
- [ ] **Fix the Known Issue:** the Vulkan fault injection fires only at frame start; add `lose_device_at_submit` and `lose_device_at_present` (and a count, so it can strike twice) to `FaultInjection`, honored by both backends, so loss mid-frame is tested on the real device too. Remove the corresponding `KNOWN-ISSUES.md` entry in this commit.
- [ ] Real loss differs from injected loss (old objects may be in a bad state): destruction after loss must not wait forever: `WaitIdle` and fence waits on a lost device return immediately; every `Destroy*` is tolerant; a timeout guard (`device_loss_destroy_timeout`) in the Vulkan backend; tested by injecting loss and then destroying with outstanding frames.
- [ ] Surface loss (`VK_ERROR_SURFACE_LOST_KHR`) and out-of-memory during recreate are separate paths with their own tests (recreate surface only; OOM fails the attempt and counts).
- [ ] Tests (null backend, deterministic): loss at frame N recovers and the next frames render; loss during recovery uses the next attempt; retry limit reached ends `DeviceLost` with the final reason; fallback adapter used when the saved one fails; loss twice in one run; loss while a settings change is pending (the change is applied by the recovery, not lost); scene identical afterwards. GPU tests: injected loss at start, submit and present on Vulkan recover; no validation errors, counts at zero.
- [ ] **Documentation:** labels; `renderer.md` and `rhi.md` "Device loss" updated (it is no longer terminal), the `RecoveryPolicy` described, and the "what the app should show" guidance; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` plus asan and tsan; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`, extra scrutiny on destruction of a lost device and on the retry state machine; record it.
- [ ] Update `STATUS.md` and `KNOWN-ISSUES.md` (remove the mid-frame-loss entry); commit.

**Done:** an injected or real device loss at any point in a frame recovers through the Day 110 reset within the retry limit or fails with a reported reason, destruction after loss never hangs, mid-frame loss is tested on Vulkan, and the guides no longer call loss terminal.
