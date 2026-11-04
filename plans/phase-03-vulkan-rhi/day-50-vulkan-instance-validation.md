# Phase 3 / Day 50 — Instance, Validation Layer & Debug Messenger

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 49.

A Vulkan instance with the right extensions, and — crucially for every later day — validation output wired into the log and the test harness.

## Tasks

- [ ] `VulkanInstance` class: create with application/engine info, API version 1.3, required extensions chosen by purpose (`VK_EXT_debug_utils` when validating; surface extensions `VK_KHR_surface` + `VK_KHR_wayland_surface`/`VK_KHR_xlib_surface` only when a presentable device is requested), refuse with a clear message if a required extension is missing.
- [ ] Validation policy from the spec: enable `VK_LAYER_KHRONOS_validation` when `ENGINE_RHI_VALIDATION` is set (or Debug default) **and** the layer is present; otherwise log a warning once and continue. Enable the synchronization-validation and best-practices features behind separate env switches (default: sync validation on in tests).
- [ ] Debug messenger: callback maps severity to `LogVerbosity` (`ERROR`→Error, `WARNING`→Warn, `INFO`→Debug, `VERBOSE`→Trace), prefixes the message id, **counts** errors/warnings in an atomic counter exposed as `ValidationCounts()`; a test hook can capture messages.
- [ ] `ENGINE_RHI_VALIDATION_FATAL=1` makes a validation error abort (for dev runs); tests instead assert `ValidationCounts().errors == 0` through a shared test fixture `GpuFixture` that every `[gpu]` test uses and that fails the test on any validation error at teardown.
- [ ] Tests `[gpu]`: instance creates and destroys cleanly with and without validation; a deliberately invalid call (e.g. creating a buffer with size 0 via the raw table) produces exactly one counted error and the capture hook sees the message id (proves the plumbing, then the call is removed from the shipped test by using the raw-table test only).
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the env switches (`ENGINE_RHI_VALIDATION`, `_FATAL`) and the fixture are documented in `docs/guide/rhi.md` and `docs/guide/building-and-testing.md`.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** instance creation works with validation on/off, validation messages reach the log and the counters, and the shared fixture turns any unexpected message into a test failure.
