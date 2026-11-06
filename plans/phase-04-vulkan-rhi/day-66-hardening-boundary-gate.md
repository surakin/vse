# Phase 4 / Day 66 — Hardening, Sanitizers & the Vulkan Boundary Gate

**Phase:** [Phase 4 — Vulkan RHI](../phase-04-vulkan-rhi.md)
**Depends on:** Day 65.

Make the milestone robust and make the architectural rule mechanically enforced.

## Tasks

- [x] Boundary gate: a `rhi_boundary_check` ctest (a small C++ tool in `tools/` using the foundation, or a CMake script) that fails if any file outside `engine/rhi/vulkan/` (and its tests, and `engine/foundation/os/gpu_driver_probe.*` — an allowed Phase 1 piece, listed explicitly) contains `vk`/`Vk`/`VK_` API identifiers or includes `vulkan`; allow-list file with reasons; a test that proves it fails on a planted violation.
- [x] Also gate the reverse dependency: nothing in `engine/rhi/*.h` (backend-agnostic headers) includes Vulkan, Wayland, X11 or backend headers; checked by the same tool.
- [x] Failure injection: device-lost simulation (fault hook) leaves the demo exiting cleanly with a message; out-of-memory path (allocator told to fail) returns invalid handles and the demo logs and continues/exits as documented; zero-size window / rapid resize storm (200 resizes) neither crashes nor leaks; present-mode fallback when MAILBOX is unavailable.
- [x] Sanitizers: `rhi_tests_asan` and `rhi_tests_tsan` (multi-threaded recording, concurrent uploads) green; leak counters zero; add `[gpu]` tests to the ASan run when a device exists (vendor drivers may report benign leaks — keep a documented suppression file `tests/rhi/lsan.supp` with a reason per entry, reviewed in the code review).
- [x] Review pass over every `ENGINE_ASSERT`/log site in `engine/rhi`: each is classified (programming error → assert, environment problem → log + invalid handle), documented in the headers (`Aborts:`/`Fails:`), and the lint stays at zero findings.
- [x] Timing: full RHI test suite (headless) under a documented time budget; GPU tests skip cleanly on a machine without Vulkan (verify by running with `ENGINE_VULKAN_LIBRARY=/nonexistent`).
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the boundary rule and the error-classification table are documented in `docs/guide/rhi.md` ("Rules & contracts"); the gate tool is documented in `docs/guide/building-and-testing.md`.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** the boundary gate runs in ctest and fails on a planted violation; sanitizer and fault-injection runs are clean; the suite passes (or skips) on a machine with no Vulkan.

## Result

**The Vulkan boundary is mechanically enforced and the error paths are exercised.**

- **Boundary gate:** `tools/rhi_boundary_check/check.cmake` + `allowlist.txt` (`path|reason`; reasonless, stale or missing entries fail) as ctest `rhi_boundary_check` (label `boundary`). It strips block, trailing and `//` comments, scans `engine/`, `game/`, `tools/`, and applies the reverse rule to `engine/rhi/*.h`. Running it on the real tree found two real violations, both fixed: `rhi_demo` used `vulkan::VulkanDevice::Create` (now `rhi::CreateDevice`), and the null backend included `rhi/vulkan/vk_array.h` (the helper became backend-neutral `engine/rhi/grow_array.h`). The only allow-listed files are `gpu_driver_probe.{h,cpp}`. Planted-violation fixtures prove the gate fails on five violations; clean, stale and reasonless fixtures cover the rest.
- **Failure injection:** `rhi::FaultInjection` (`lose_device_at_frame`, `fail_allocations_from`) in `DeviceCreateInfo`, honored by Vulkan (the null backend honors the first). `rhi_demo --inject-device-loss F` exits 3 with a message; `--inject-oom N` exits 1 cleanly, whether creation or a later resource fails. `tests/rhi/vulkan_fault_test.cpp` (`[fault]`): device lost at a frame, creation OOM, later-resource OOM with the device still running, and a 200-resize storm (zero and 1x1 sizes, Mailbox requested) on Wayland and X11, each ending with zero live objects and a silent validation layer. Demo ctests check exit codes and messages; mistyped injections are usage errors.
- **Sanitizers:** ASan 286/286 with GPU tests, TSan 219/219 (GPU-free, as before). Leak suppressions moved to `tests/rhi/lsan.supp` with a reason per entry, loaded through `__lsan_default_options`; verified the file is effective (without the messenger entry the deliberate leak is reported).
- **Assert/log review:** all 149 `ENGINE_ASSERT` sites and the log sites in `engine/rhi` classified: operations on an existing handle assert (programming error); `Create*` with a bad description and environment failures log and return invalid/false/0. No site needed to change class. The table is in `docs/guide/rhi.md` ("Rules & contracts").
- **Timing / no-Vulkan:** `ctest -L rhi -j4`: ~15 s with a GPU, ~1 s with `ENGINE_VULKAN_LIBRARY=/nonexistent` (142 GPU tests skip). The check found one real bug: the triangle golden test failed instead of skipping, because `SKIP` was nested inside `REQUIRE(...)`.
- **Docs:** boundary rule, error-classification table and fault injection in `docs/guide/rhi.md`; gate, fault tests, sanitizers and the time budget in `docs/guide/building-and-testing.md`; `docs/reference/` regenerated (1689/1689, lint at zero).
- Full suite: 1277/1277 with `ENGINE_RHI_REQUIRE_GPU=1`.

## Post-implementation code review

`/code-review high` returned 10 findings.

Fixed: (1) `rhi_demo_oom_device` passed on a machine with no GPU and `oom_target` skipped when injection landed in creation: the tests now expect distinct messages ("cannot set up the staging ring"; either clean failure) and skip only on loader/device-absent output. (2) The boundary scanner mistook block-comment interiors and trailing comments for code: it now strips comments properly (new fixtures). (3) The OOM test probe loop is bounded and requires a found creation count. (4) Fault options were parsed with `atol` and cast to `u32`: now a strict parser, bad values are usage errors (tests). (5) A device lost in the last screenshot frame exited 1: `DeviceLost()` is checked after the loop (exit 3). (6) `expect_run.cmake` treated an empty `ENGINE_RHI_REQUIRE_GPU` as set, unlike `GpuRequired()`: same rule now. (7) `FaultInjection` was silently ignored by the null backend: `lose_device_at_frame` is now honored (test), the other documented as not applicable. (8) The resize-storm name overclaimed: renamed "resize requests" (see declined).

Declined: (a) Skipping the per-frame pool resets after an injected loss is the same path a real `VK_ERROR_DEVICE_LOST` takes, which is what the fault is meant to exercise; teardown after the loss is covered by the test with zero live objects and no validation errors. (b) `leak:libX11`/`libxcb` also hide engine X11 leaks: the entries predate this day and cannot be narrowed (the driver's frames are anonymous modules); a caveat is now recorded in `lsan.supp` and X11 window code is covered by the plain/TSan flavors. Also declined: a window-system-driven resize test and a forced "no MAILBOX" device test: `os::Window` has no programmatic resize, and the fallback is covered by the `ChoosePresentMode` policy test plus the storm's Mailbox request; strings in the gate are scanned on purpose (`dlsym("vkFoo")` is API use).
