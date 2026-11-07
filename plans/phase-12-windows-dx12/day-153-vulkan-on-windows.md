# Phase 12 / Day 153 — Vulkan on Windows: Loader, Surface, RHI Tests and Golden Images

**Phase:** [Phase 12 — Windows Port + DX12 Backend](../phase-12-windows-dx12.md)
**Depends on:** Days 151-152; the Vulkan SDK installed (Day 140).

Run the existing Vulkan backend on Windows. This proves the window, the RHI tests and the golden images on Windows before DX12 exists, and it is the first time
the goldens (blessed on an NVIDIA RTX 3060) are compared on an AMD GPU.

## Tasks

- [ ] **Loader:** `vk_loader.cpp` loads `vulkan-1.dll` on Windows (through `os::LoadLibrary`, so the search order excludes the current directory) and
      `libvulkan.so.1` on Linux; the name is chosen per platform in one place; `vulkan_loader_test` runs on both.
- [ ] **Surface:** `VK_KHR_win32_surface` from `NativeWindowKind::Win32` (`vkCreateWin32SurfaceKHR` with the `HINSTANCE`/`HWND`); presentation support with
      `vkGetPhysicalDeviceWin32PresentationSupportKHR`; the Wayland and Xlib paths stay Linux-only sources; the `vk_functions.inc` table gains the Win32 entries
      behind the platform rule (no `#ifdef` inside the shared table body if the generator can split it — decide).
- [ ] **Validation layers** from the SDK when `--validation` is on; the validation-message check the Linux tests use stays the oracle (silent means zero
      messages at warning or above); `ENGINE_RHI_VALIDATION*` behave the same.
- [ ] **Pipeline cache and caches directory** under `UserCacheDirectory()` on Windows (file named by adapter identity, as on Linux).
- [ ] **The Phase 4 RHI suite on Windows through Vulkan** (`tests/rhi/vulkan_*`): every test builds and runs; record per test pass/skip/fail. Linux-only parts
      (`lsan` options, the Xlib surface test) are excluded by the platform rule, not deleted.
- [ ] **Golden images on the AMD iGPU:** triangle, cube, textured cube, lit cube (and compute) compared with the existing images and tolerances. If a comparison
      fails: record the measured differences (max, mean, pixel count over tolerance, and where), investigate the cause (precision, filtering, rasterization
      rules, driver), and either fix a real bug or change the tolerance with the numbers and the reason written in the test and in `KNOWN-ISSUES.md`. **Never
      re-bless an image** to make a platform pass.
- [ ] **Memory and device:** `vk_memory` heap selection on a UMA device (integrated GPU: device-local and host-visible heaps coincide) — check the selection
      policy picks sensibly and record what it chose.
- [ ] **Documentation:** `docs/guide/rhi.md` "Vulkan on Windows" (SDK, layers, the loader); reference regenerated if headers changed; `docgen lint` zero.
- [ ] **Runs:** Windows `--affected` plain and asan (the RHI selection); Linux `--affected` plus asan and tsan (shared Vulkan backend files changed); quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: surface lifetime against the `HWND` (window destroyed before the surface), and any tolerance change.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the Vulkan backend runs on Windows through a Win32 surface with validation silent, the Phase 4 RHI suite passes on Windows, and every golden image
matches within a tolerance that is either unchanged or changed with recorded measurements.
