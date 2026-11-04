# Phase 3 / Day 66 — Hardening, Sanitizers & the Vulkan Boundary Gate

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 65.

Make the milestone robust and make the architectural rule mechanically enforced.

## Tasks

- [ ] Boundary gate: a `rhi_boundary_check` ctest (a small C++ tool in `tools/` using the foundation, or a CMake script) that fails if any file outside `engine/rhi/vulkan/` (and its tests, and `engine/foundation/os/gpu_driver_probe.*` — an allowed Phase 1 piece, listed explicitly) contains `vk`/`Vk`/`VK_` API identifiers or includes `vulkan`; allow-list file with reasons; a test that proves it fails on a planted violation.
- [ ] Also gate the reverse dependency: nothing in `engine/rhi/*.h` (backend-agnostic headers) includes Vulkan, Wayland, X11 or backend headers; checked by the same tool.
- [ ] Failure injection: device-lost simulation (fault hook) leaves the demo exiting cleanly with a message; out-of-memory path (allocator told to fail) returns invalid handles and the demo logs and continues/exits as documented; zero-size window / rapid resize storm (200 resizes) neither crashes nor leaks; present-mode fallback when MAILBOX is unavailable.
- [ ] Sanitizers: `rhi_tests_asan` and `rhi_tests_tsan` (multi-threaded recording, concurrent uploads) green; leak counters zero; add `[gpu]` tests to the ASan run when a device exists (vendor drivers may report benign leaks — keep a documented suppression file `tests/rhi/lsan.supp` with a reason per entry, reviewed in the code review).
- [ ] Review pass over every `ENGINE_ASSERT`/log site in `engine/rhi`: each is classified (programming error → assert, environment problem → log + invalid handle), documented in the headers (`Aborts:`/`Fails:`), and the lint stays at zero findings.
- [ ] Timing: full RHI test suite (headless) under a documented time budget; GPU tests skip cleanly on a machine without Vulkan (verify by running with `ENGINE_VULKAN_LIBRARY=/nonexistent`).
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the boundary rule and the error-classification table are documented in `docs/guide/rhi.md` ("Rules & contracts"); the gate tool is documented in `docs/guide/building-and-testing.md`.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** the boundary gate runs in ctest and fails on a planted violation; sanitizer and fault-injection runs are clean; the suite passes (or skips) on a machine with no Vulkan.
