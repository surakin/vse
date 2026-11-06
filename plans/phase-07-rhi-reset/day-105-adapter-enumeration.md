# Phase 7 / Day 105 — Adapter Enumeration & Selection

**Phase:** [Phase 7 — RHI Reset](../phase-07-rhi-reset.md)
**Depends on:** Days 103-104.

Today the GPU is chosen inside `CreateDevice` by a ranking, with `ENGINE_RHI_GPU` as the only override. A user-changeable
GPU setting needs a list to show and an explicit choice to pass in.

## Tasks

- [ ] `engine/rhi/adapter.h`: `AdapterInfo` (stable id string, display name, kind discrete/integrated/virtual/software, dedicated memory bytes, driver description, whether it can present to the given window, whether it meets the engine's requirements and, if not, why in a short static string); `AdapterList` as a fixed-capacity array (same 16 limit as `kMaxPhysicalDevices`).
- [ ] `rhi::EnumerateAdapters(const DeviceCreateInfo&, AdapterList*)`: Vulkan creates a throwaway instance (no device), queries physical devices and presentability against the window's surface, destroys it; the null backend returns two fake adapters (one discrete, one integrated) so most tests need no GPU; `Backend::Null` honors a test hook that sets the list.
- [ ] `DeviceCreateInfo` gains `adapter` (an id string, null for "best"); precedence documented: explicit `adapter` > `ENGINE_RHI_GPU` > ranking. A requested adapter that does not exist or does not qualify fails with a log line naming the reason and does **not** silently fall back (the caller decides, Day 110/111).
- [ ] The adapter id is stable across runs on one machine (vendor id, device id and the driver's UUID where there is one), not the enumeration index, so a saved setting survives a GPU being added.
- [ ] `vk_device_select`: reuse the pure selection policy; the new function only adds the id matching and the unsuitable-reason strings. Existing selection tests keep passing unchanged.
- [ ] Capabilities for Day 104: `AdapterCount`, and `Capabilities` filled from the adapter list.
- [ ] Tests: null-backend enumeration; id match by id, by name substring and by index; precedence over `ENGINE_RHI_GPU`; unknown id fails with the reason; the id is identical on two enumerations; a pure-policy test for the new unsuitable-reason strings. A GPU test (label `gpu`, skips without a GPU) lists real adapters and creates a device on each presentable one, rendering one frame to a texture; on a single-adapter machine it runs once and says so.
- [ ] **Documentation:** `///` comments with contract labels; `docs/guide/rhi.md` gets an "Choosing a GPU" section with a tagged sample; Vulkan-only names stay out of `adapter.h` (the boundary gate passes); reference regenerated and committed; `docgen lint` zero; `ctest -L docs`.
- [ ] Portability note in the day's result: how DX12 (`IDXGIFactory6::EnumAdapterByGpuPreference`, LUID as id) and Metal (`MTLCopyAllDevices`, registry id) map to `AdapterInfo`; copy into the Phase 8 and 9 plan files.
- [ ] Run `scripts/test-phase.sh --affected`; quote the line.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; record it.
- [ ] Update `STATUS.md`; commit.

**Done:** a caller can list adapters, pick one by id when creating a device, and get a precise failure if it cannot be used; the null backend makes this testable without a GPU; the guide section and Phase 8-9 notes are committed.
