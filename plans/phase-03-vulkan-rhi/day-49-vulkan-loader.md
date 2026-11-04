# Phase 3 / Day 49 — Vulkan Loader & Function Tables

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Days 45, 47.

Everything the backend calls is resolved at runtime into explicit tables; nothing links against libvulkan.

## Tasks

- [x] `engine/rhi/vulkan/vk_loader.{h,cpp}`: open `libvulkan.so.1` via `os::LoadLibrary`, resolve `vkGetInstanceProcAddr`, expose `VulkanLoader::Load()` returning a status (`NoLoader`, `Ok`) and logging the reason; the library handle is intentionally never unloaded (as in Day 17) — document why.
- [x] Function tables as plain structs of `PFN_vk*` pointers: `VkGlobalFns` (`vkCreateInstance`, `vkEnumerateInstance{Version,ExtensionProperties,LayerProperties}`), `VkInstanceFns` (physical-device queries, surface and debug-utils functions, `vkCreateDevice`, `vkGetDeviceProcAddr`), `VkDeviceFns` (everything the backend will use through Day 65). Loader functions `LoadGlobalFns`, `LoadInstanceFns(instance)`, `LoadDeviceFns(device)`; a single X-list (`vk_functions.inc`, expanded by a file-local macro into the struct members and the name/offset tables) so adding a function is one line — chosen over a hand-maintained name array so the member and its name string cannot drift apart; a missing mandatory function is a hard error naming it, a missing optional one a null pointer.
- [x] `ENGINE_VK_CHECK`-style result helper: `VkResult` → readable name (`VkResultName`) → logged with file/line on the `LogRhi` channel; classification into success / recoverable (`OUT_OF_DATE`, `SUBOPTIMAL`) / device-lost / fatal.
- [x] Query and log `vkEnumerateInstanceVersion` (require ≥ 1.3, otherwise a clear "driver too old" failure), the instance extension and layer lists.
- [x] Tests `[gpu]`: loader loads and reports a version ≥ 1.3 on the dev machine; with `ENGINE_VULKAN_LIBRARY=/nonexistent` (test override) the loader reports `NoLoader` and the test SKIPs rather than crashes; `VkResultName` covers every enumerator used.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** add the `rhi/vulkan` module (`header engine/rhi/vulkan/*.h`, non-strict) to `docs/reference/modules.txt`; the (internal) backend headers are documented as its members; the guide notes the loader policy under "Vulkan backend notes".
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** the loader reaches `vkCreateInstance` and the enumerate functions through our tables on this machine and degrades to a clean "no Vulkan" status elsewhere.

## Result

`engine/rhi/vulkan/`: `vk_loader` (library open, `ENGINE_VULKAN_LIBRARY` override, `NoLoader`/`BadLoader`), `vk_functions` (three tables generated from `vk_functions.inc`, `Load*Fns`, `CheckInstanceVersion`, `LogInstanceCapabilities`), `vk_result` (`VkResultName`, `ClassifyVkResult`, `ENGINE_VK_CHECK`). The old placeholder `vulkan_backend.cpp` is gone. New docs module `rhi-vulkan`, guide section "Vulkan backend notes". 10 test cases (72 assertions) pass plain and under ASan, including the two `[gpu]` cases on the dev machine's real NVIDIA driver (also with `ENGINE_RHI_REQUIRE_GPU=1`). The ASan test binary suppresses leak reports from libdbus/driver libraries (`__lsan_default_suppressions`): they come from the NVIDIA ICD, not engine code.

`vkCreateXlibSurfaceKHR` is deliberately not in the tables yet: `vulkan_xlib.h` includes `<X11/Xlib.h>`, whose `Success`/`None` macros break C++ enums. Day 56 adds it from an isolated TU (note added there).

## Post-implementation code review

`/code-review high`, 9 findings:

1. **GPU tests hard-failed on a loader older than 1.3** — fixed: skip (fail under `ENGINE_RHI_REQUIRE_GPU=1`); the device test also skips on a device below 1.3.
2. **vkCreateDevice failure always SKIPped and family 0 hard-coded** — fixed: picks a graphics+compute queue family, and a `CreateDevice` failure on a 1.3 device is a `REQUIRE` failure (logged through `ENGINE_VK_CHECK`).
3. **Tests asserted unenabled-extension functions resolve to null** (loader-dependent) — fixed: removed; the optional/mandatory contract is tested deterministically with a fake `vkGetInstanceProcAddr` (4 sections).
4. **Wayland define/entry unguarded in common code** — fixed: define only on Linux, and the `.inc` entry is guarded on `VK_KHR_wayland_surface`, so other platforms add their own define and line.
5. **Doc/implementation mismatches; partially filled table on failure** — fixed: docs say every missing function is logged; a failed load now leaves the table all-null (regression test); `CheckInstanceVersion` docs corrected.
6. **Instance version vs. physical-device version** — declined for this day (no devices are enumerated yet); contract narrowed in the docs and a task added to Day 50.
7. **`ENGINE_VULKAN_LIBRARY` honored in all builds; BadLoader leaked the handle** — leak fixed (the foreign library is unloaded). Env override kept: `LD_PRELOAD`/`VK_ICD_FILENAMES` give the same control, so it grants nothing new; documented, plus the getenv/setenv caveat.
8. **SUBOPTIMAL and OUT_OF_DATE collapsed; SURFACE_LOST Fatal** — fixed: new `Suboptimal` class; `SURFACE_LOST` is `Recoverable`. Declined: `OUT_OF_DEVICE_MEMORY` stays `Fatal` at this layer (callers that can react keep the raw `VkResult`; documented).
9. **~100 KB stack in `LogInstanceCapabilities`, errors ignored** — fixed: 64 extensions / 32 layers in separate scopes (~17 KB live), enumeration failures logged. Declined: unbounded two-call pattern (needs an allocator for a debug log).

The review's tenth note (STATUS and day-file bookkeeping) is done in this commit.
