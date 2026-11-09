# Phase 8 / Day 105 — Adapter Enumeration & Selection

**Phase:** [Phase 8 — RHI Reset](../phase-08-rhi-reset.md)
**Depends on:** Days 103-104.

Today the GPU is chosen inside `CreateDevice` by a ranking, with `ENGINE_RHI_GPU` as the only override. A user-changeable
GPU setting needs a list to show and an explicit choice to pass in.

## Tasks

- [x] `engine/rhi/adapter.h`: `AdapterInfo` (stable id string, display name, kind discrete/integrated/virtual/software, dedicated memory bytes, driver description, whether it can present to the given window, whether it meets the engine's requirements and, if not, why in a short static string); `AdapterList` as a fixed-capacity array (same 16 limit as `kMaxPhysicalDevices`).
- [x] `rhi::EnumerateAdapters(const DeviceCreateInfo&, AdapterList*)`: Vulkan creates a throwaway instance (no device), queries physical devices and presentability against the window's surface, destroys it; the null backend returns two fake adapters (one discrete, one integrated) so most tests need no GPU; `Backend::Null` honors a test hook that sets the list.
- [x] `DeviceCreateInfo` gains `adapter` (an id string, null for "best"); precedence documented: explicit `adapter` > `ENGINE_RHI_GPU` > ranking. A requested adapter that does not exist or does not qualify fails with a log line naming the reason and does **not** silently fall back (the caller decides, Day 110/111).
- [x] The adapter id is stable across runs on one machine (vendor id, device id and the driver's UUID where there is one), not the enumeration index, so a saved setting survives a GPU being added.
- [x] `vk_device_select`: reuse the pure selection policy; the new function only adds the id matching and the unsuitable-reason strings. Existing selection tests keep passing unchanged.
- [x] Capabilities for Day 104: `AdapterCount`, and `Capabilities` filled from the adapter list.
- [x] Tests: null-backend enumeration; id match by id, by name substring and by index; precedence over `ENGINE_RHI_GPU`; unknown id fails with the reason; the id is identical on two enumerations; a pure-policy test for the new unsuitable-reason strings. A GPU test (label `gpu`, skips without a GPU) lists real adapters and creates a device on each presentable one, rendering one frame to a texture; on a single-adapter machine it runs once and says so.
- [x] **Documentation:** `///` comments with contract labels; `docs/guide/rhi.md` gets an "Choosing a GPU" section with a tagged sample; Vulkan-only names stay out of `adapter.h` (the boundary gate passes); reference regenerated and committed; `docgen lint` zero; `ctest -L docs`.
- [x] Portability note in the day's result: how DX12 (`IDXGIFactory6::EnumAdapterByGpuPreference`, LUID as id) and Metal (`MTLCopyAllDevices`, registry id) map to `AdapterInfo`; copy into the Phase 13 and 23 plan files.
- [x] Run `scripts/test-phase.sh --affected`; quote the line.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`; record it.
- [x] Update `STATUS.md`; commit.

**Done:** a caller can list adapters, pick one by id when creating a device, and get a precise failure if it cannot be used; the null backend makes this testable without a GPU; the guide section and Phases 13 and 23 notes are committed.

## Result

Added `engine/rhi/adapter.h/.cpp` (`AdapterInfo`, `AdapterList`, `EnumerateAdapters`, `MatchAdapter`, null-backend list and `SetNullAdapters` hook), `DeviceCreateInfo::adapter`, `Device::Adapter()`, Vulkan ids (`vk-<vendor>-<device>-<uuid>`), `SelectPhysicalDeviceFor`, `Capabilities::SetAdapterCount`. Tests: `tests/rhi/adapter_test.cpp` (12 cases incl. GPU) + 1 in render_settings_test. Guide: `rhi.md` "Choosing a GPU" (docs sample links `rhi_vulkan` now). Portability notes appended to the Phase 13 and 23 plans.
Scoped runs: plain `ran 937 of 2477 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 668 of 2477 ... flavors: asan`, tsan `ran 400 of 2477 ... flavors: tsan`; all pass (re-run after review fixes: `ran 33 of 2480 tests (phases: 7; flavors: plain ...)`). `ctest -L docs` 4/4.

## Post-implementation code review

`/code-review high`: 9 findings. Fixed: `multi_adapter` is now derived from the count (a smaller count removes it); null backend fails when no adapter is suitable (test added); NVIDIA driver version decoded with its own bit layout; zero-UUID id collision documented in `adapter.h`; GPU test restores `ENGINE_RHI_GPU`; bookkeeping done. Declined: clamp-constant mismatch and per-call AdapterInfo building in selection (both 16, cost negligible, selection runs once per device); enumeration cost per call (documented as a throwaway instance; callers cache); duplicate null branches (small, two backends); null-adapter hook in the public header (needed by tests in other modules; the guard restores it).
