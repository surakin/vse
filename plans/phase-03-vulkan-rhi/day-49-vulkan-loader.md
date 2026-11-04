# Phase 3 / Day 49 — Vulkan Loader & Function Tables

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Days 45, 47.

Everything the backend calls is resolved at runtime into explicit tables; nothing links against libvulkan.

## Tasks

- [ ] `engine/rhi/vulkan/vk_loader.{h,cpp}`: open `libvulkan.so.1` via `os::LoadLibrary`, resolve `vkGetInstanceProcAddr`, expose `VulkanLoader::Load()` returning a status (`NoLoader`, `Ok`) and logging the reason; the library handle is intentionally never unloaded (as in Day 17) — document why.
- [ ] Function tables as plain structs of `PFN_vk*` pointers: `VkGlobalFns` (`vkCreateInstance`, `vkEnumerateInstance{Version,ExtensionProperties,LayerProperties}`), `VkInstanceFns` (physical-device queries, surface and debug-utils functions, `vkCreateDevice`, `vkGetDeviceProcAddr`), `VkDeviceFns` (everything the backend will use through Day 65). Loader functions `LoadGlobalFns`, `LoadInstanceFns(instance)`, `LoadDeviceFns(device)`; a macro-free X-list or table-driven name array so adding a function is one line; a missing mandatory function is a hard error naming it, a missing optional one a null pointer.
- [ ] `ENGINE_VK_CHECK`-style result helper: `VkResult` → readable name (`VkResultName`) → logged with file/line on the `LogRhi` channel; classification into success / recoverable (`OUT_OF_DATE`, `SUBOPTIMAL`) / device-lost / fatal.
- [ ] Query and log `vkEnumerateInstanceVersion` (require ≥ 1.3, otherwise a clear "driver too old" failure), the instance extension and layer lists.
- [ ] Tests `[gpu]`: loader loads and reports a version ≥ 1.3 on the dev machine; with `ENGINE_VULKAN_LIBRARY=/nonexistent` (test override) the loader reports `NoLoader` and the test SKIPs rather than crashes; `VkResultName` covers every enumerator used.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the (internal) backend headers are documented as `rhi/vulkan` module members; the guide notes the loader policy under "Vulkan backend notes".
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** the loader reaches `vkCreateInstance` and the enumerate functions through our tables on this machine and degrades to a clean "no Vulkan" status elsewhere.
