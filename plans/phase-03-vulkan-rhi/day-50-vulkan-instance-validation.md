# Phase 3 / Day 50 — Instance, Validation Layer & Debug Messenger

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 49.

A Vulkan instance with the right extensions, and — crucially for every later day — validation output wired into the log and the test harness.

## Tasks

- [x] `VulkanInstance` class: create with application/engine info, API version 1.3, required extensions chosen by purpose (`VK_EXT_debug_utils` when validating; surface extensions `VK_KHR_surface` + `VK_KHR_wayland_surface`/`VK_KHR_xlib_surface` only when a presentable device is requested), refuse with a clear message if a required extension is missing.
- [x] Validation policy from the spec: enable `VK_LAYER_KHRONOS_validation` when `ENGINE_RHI_VALIDATION` is set (or Debug default) **and** the layer is present; otherwise log a warning once and continue. Enable the synchronization-validation and best-practices features behind separate env switches (default: sync validation on in tests).
- [x] Debug messenger: callback maps severity to `LogVerbosity` (`ERROR`→Error, `WARNING`→Warn, `INFO`→Debug, `VERBOSE`→Trace), prefixes the message id, **counts** errors/warnings in an atomic counter exposed as `ValidationCounts()`; a test hook can capture messages.
- [x] `ENGINE_RHI_VALIDATION_FATAL=1` makes a validation error abort (for dev runs); tests instead assert `ValidationCounts().errors == 0` through a shared test fixture `GpuFixture` that every `[gpu]` test uses and that fails the test on any validation error at teardown.
- [x] Tests `[gpu]`: instance creates and destroys cleanly with and without validation; a deliberately invalid call (e.g. creating a buffer with size 0 via the raw table) produces exactly one counted error and the capture hook sees the message id (proves the plumbing, then the call is removed from the shipped test by using the raw-table test only).
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the env switches (`ENGINE_RHI_VALIDATION`, `_FATAL`) and the fixture are documented in `docs/guide/rhi.md` and `docs/guide/building-and-testing.md`.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** instance creation works with validation on/off, validation messages reach the log and the counters, and the shared fixture turns any unexpected message into a test failure.

## Result

`engine/rhi/vulkan/vk_instance.{h,cpp}`: `VulkanInstance` (Vulkan 1.3; debug-utils, surface and platform-surface extensions by purpose; validation layer and `VK_EXT_validation_features` for sync/best-practices), the pure `ResolveValidationPolicy`, process-wide `GetValidationCounts`/`ResetValidationCounts`, and the `SetValidationCapture` hook. The test fixture `gpu_test::GpuFixture` (`tests/rhi/gpu_fixture.h`) is used by every `[gpu]` test, including the Day 49 device test. Guides: validation section in `docs/guide/rhi.md`, GPU-test section in `docs/guide/building-and-testing.md`. The deliberately invalid call is `vkEnumeratePhysicalDevices` with a null count (there is no device yet for the planned zero-size buffer; the loader itself reports that error, id `Loader Message`, VUID in the text). 16 test cases in `[vulkan]`, plain and ASan, also with `ENGINE_RHI_REQUIRE_GPU=1`; docs 1270/1270.

The Day 49 carry-over (reject devices below 1.3) moved to Day 51, where devices are first enumerated.

## Post-implementation code review

`/code-review high`, 9 findings:

1. **Messenger destroyed before `vkDestroyInstance`, nothing chained into `vkCreateInstance`** — fixed: the messenger create-info is chained into the instance create info; regression test leaks a messenger and asserts the destroy-time error is counted (verified to fail without the chain).
2. **Instance claimed validation after the messenger failed** — fixed: a validation instance whose messenger cannot be created is a failed `Create`.
3. **`g_fatal` process-wide** — fixed: the setting is read before creation and carried per instance in the messenger's user data.
4. **Capture mutex held while the hook runs (deadlock risk)** — fixed: hook copied under the lock and called outside it; docs note that an in-flight call can outlive `SetValidationCapture(nullptr)`.
5. **Two tests skipped instead of failing under `ENGINE_RHI_REQUIRE_GPU=1`; copy-pasted block** — fixed: `GpuFixture::RequireValidation()`.
6. **Duplicate `GpuRequired()`** — fixed: the loader test uses the fixture's.
7. **Fixture destructor checked counts while unwinding** — fixed: skipped when `std::uncaught_exceptions() != 0`.
8. **Loader lifetime docs wrong (it is copied)** — fixed.
9. Declined: raw `new[]` in `ExtensionList`/`HasLayer` — short-lived driver query buffers on a cold path, same as the rest of `rhi`, and the foundation has no general-purpose allocator API for this. Declined: STL in the test fixture — tests are built with the STL by design (Catch2); only production code is STL-free.
