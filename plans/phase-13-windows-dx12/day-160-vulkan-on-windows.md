# Phase 13 / Day 160 — Vulkan on Windows: Loader, Surface, RHI Tests and Golden Images

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Days 158-159; the Vulkan SDK installed (Day 147).

Run the existing Vulkan backend on Windows. This proves the window, the RHI tests and the golden images on Windows before DX12 exists, and it is the first time
the goldens (blessed on an NVIDIA RTX 3060) are compared on an AMD GPU.

## Tasks

- [x] **Loader:** `vk_loader.cpp` loads `vulkan-1.dll` on Windows (through `os::LoadLibrary`, so the search order excludes the current directory) and
      `libvulkan.so.1` on Linux; the name is chosen per platform in one place; `vulkan_loader_test` runs on both.
- [x] **Surface:** `VK_KHR_win32_surface` from `NativeWindowKind::Win32` (`vkCreateWin32SurfaceKHR` with the `HINSTANCE`/`HWND`); presentation support with
      `vkGetPhysicalDeviceWin32PresentationSupportKHR`; the Wayland and Xlib paths stay Linux-only sources; the `vk_functions.inc` table gains the Win32 entries
      behind the platform rule (no `#ifdef` inside the shared table body if the generator can split it — decide).
      *(Decided: no entry in the shared table. `vkCreateWin32SurfaceKHR` is resolved in `rhi/vulkan/windows/vk_win32_surface.cpp`, as
      `vkCreateXlibSurfaceKHR` is in its own file, because `vulkan_win32.h` needs `<windows.h>`. Presentation support is asked through the
      surface (`vkGetPhysicalDeviceSurfaceSupportKHR`), as on Linux, so the Win32-specific query is not used.)*
- [x] **Validation layers** from the SDK when `--validation` is on; the validation-message check the Linux tests use stays the oracle (silent means zero
      messages at warning or above); `ENGINE_RHI_VALIDATION*` behave the same.
- [x] **Pipeline cache and caches directory** under `UserCacheDirectory()` on Windows (file named by adapter identity, as on Linux).
- [x] **The Phase 4 RHI suite on Windows through Vulkan** (`tests/rhi/vulkan_*`): every test builds and runs; record per test pass/skip/fail. Linux-only parts
      (`lsan` options, the Xlib surface test) are excluded by the platform rule, not deleted.
- [x] **Golden images on the AMD iGPU:** triangle, cube, textured cube, lit cube (and compute) compared with the existing images and tolerances. If a comparison
      fails: record the measured differences (max, mean, pixel count over tolerance, and where), investigate the cause (precision, filtering, rasterization
      rules, driver), and either fix a real bug or change the tolerance with the numbers and the reason written in the test and in `KNOWN-ISSUES.md`. **Never
      re-bless an image** to make a platform pass.
      *(Every image matched with the tolerances unchanged; nothing to investigate, nothing re-blessed.)*
- [x] **Memory and device:** `vk_memory` heap selection on a UMA device (integrated GPU: device-local and host-visible heaps coincide) — check the selection
      policy picks sensibly and record what it chose.
- [x] **Documentation:** `docs/guide/rhi.md` "Vulkan on Windows" (SDK, layers, the loader); reference regenerated if headers changed; `docgen lint` zero.
- [x] **Runs:** Windows `--affected` plain and asan (the RHI selection); Linux `--affected` plus asan and tsan (shared Vulkan backend files changed); quote the lines.
      *(Windows done; the Linux run waits for the phase close, Day 174.)*
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: surface lifetime against the `HWND` (window destroyed before the surface), and any tolerance change.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the Vulkan backend runs on Windows through a Win32 surface with validation silent, the Phase 4 RHI suite passes on Windows, and every golden image
matches within a tolerance that is either unchanged or changed with recorded measurements.

## Notes (2026-11-08, Windows PC; AMD Radeon 860M, integrated, driver 26.10.1, Vulkan 1.4)

- **Loader:** `vk_loader.cpp` opens `os::VulkanLoaderName()` (`vulkan-1.dll`, `libvulkan.so.1`; already the one place the name is chosen, for the
  GPU driver probe) through `os::LoadLibrary`, whose Windows search order never includes the current directory. `vulkan_loader_test` runs on
  Windows; its "not a loader" case loads `kernel32.dll` there instead of `libm.so.6`.
- **Surface:** `VK_KHR_win32_surface` is enabled with the other platform surface extensions; `CreateWin32Surface`
  (`rhi/vulkan/windows/vk_win32_surface.h/.cpp`) makes the surface from the window's `HINSTANCE` and `HWND`. The file includes
  `os/windows/win32.h`, so `rhi/vulkan/windows/` is a platform directory in `module_deps_check`, an owner of `os/windows/`, and a Windows
  directory in `platform_neutral_check` rule 4; `ARCHITECTURE.md` section 4 says so. `vk_xlib_surface.cpp` moved to the Linux source list (it was
  built everywhere), and `vulkan_device.cpp`'s unused include of its header went (review).
- **Validation layers** come from the Vulkan SDK's registration; `GpuFixture`'s oracle (zero messages at warning or above) holds in every test,
  and no test skipped for a missing layer.
- **Pipeline cache:** shared code (`ChoosePipelineCacheDir`, then `io::UserCacheDirectory`): on Windows
  `%LOCALAPPDATA%\<app>\cache\pipeline_cache_<vendor>_<device>_<uuid>.bin`. Seen by accident: running `rhi_tests.exe` by hand without
  `ENGINE_RHI_CACHE_DIR` wrote `C:\Users\<user>\AppData\Local\engine\cache\pipeline_cache_1002_1114_52893ef9...bin`; the folder (created by that
  run) was deleted. ctest sets `ENGINE_RHI_CACHE_DIR` for every RHI test.
- **The RHI suite** (`tests/rhi`) builds on Windows, plain and ASan. Ported: `adapter_test.cpp` and `vulkan_loader_test.cpp` set variables through
  `test_env.h`; `image_compare.cpp` takes the process id from `_getpid`; `types_test.cpp` uses `std::is_same_v` and a quiet NaN (cl rejects
  `__is_same` and `0.0f / 0.0f`); `vulkan_pipeline_test.cpp` truncates through `io::File::Truncate` and skips its read-only-directory case on
  Windows (filed); `vulkan_reset_golden_test.cpp`'s `/proc` measures return 0 on Windows (filed for Day 161); two names that shadowed others in a
  unity batch (C4459, C4458) were renamed; `lsan_options.cpp` is Linux-only (MSVC's ASan has no leak checker). `tests/rhi` links the renderer
  libraries, so `engine/renderer` is built on Windows from today (its own tests: Day 161).
- **Results, plain, on the Radeon 860M:** 289 tests, 281 passed, 8 skipped: the Wayland and X11 presentation cases (`Vulkan swapchain presents
  to a Wayland/X11 window`, the frame loop, the reconfigure and the 200-resize cases, two each). Their Win32 counterparts were added (the same
  checks through `PlatformWindowHost` with `WindowBackend::Win32`) and pass: `Vulkan swapchain presents to a Win32 window`, `The frame loop on a
  Win32 window keeps frames in flight bounded and leaks nothing`, `A Win32 swapchain reconfigures and its surface is replaced`, `A swapchain
  survives 200 resize requests including zero sizes on Win32`. Win32 behaves like X11 for the extent: the swapchain follows the window's size.
  The ASan flavor passes too.
- **Golden images:** the triangle, the cube, the textured cube, the lit cube and the demo scene match their committed images, and the compute
  readback its expected values, with the tolerances unchanged: the first comparison of the RTX 3060's goldens on an AMD GPU (`KNOWN-ISSUES.md`'s
  Phase 5 entry updated).
- **Found by the runs, fixed:** `Vulkan: many frames reuse the per-thread pools` failed about 1 run in 20: after `WaitIdle`, `CompletedFence()`
  read 20 while the last `Submit` returned 21. With validation off it failed 79 runs in 80; the counter read 21 about 100 us later. The device
  being idle means every signal has completed, but this driver shows the last one late, so `WaitIdle` now also waits on the timeline
  (`vkWaitSemaphores` for the last submitted value; returns at once on other drivers). Regression test: `Vulkan: after WaitIdle the completed
  fence is the last submitted one` (without validation; 9 of 10 rounds fail without the fix). The other `vkDeviceWaitIdle` calls release
  resources without reading the timeline, so they are not affected.
- **Memory on this UMA device:** the driver reports two heaps (heap 1, 31.75 GiB, device-local; heap 0, 15.87 GiB, host) and AMD's
  device-coherent uncached types beside the usual ones. `SelectMemoryType` picks: GPU-only from type 0 (device-local, not host visible, heap 1),
  uploads (`CpuToGpu`) from type 1 (host visible and coherent, not device-local, heap 0), readbacks (`GpuToCpu`) from type 3 (host visible,
  coherent and cached, heap 0); the uncached device-coherent types (4 to 7) are never chosen. Sensible. Type 2 (device-local and
  host-visible, the usual choice for uploads on an integrated GPU) is used by no location: a choice of the policy, unchanged.
- **Docs:** `docs/guide/rhi.md` "Vulkan on Windows" and the loader and platform notes; `vk_loader.h` comments; the reference (the new header).
- **For the Linux run at the phase close:** `engine/rhi/CMakeLists.txt`, `engine/renderer/CMakeLists.txt`, `engine/rhi/vulkan/vk_loader.{h,cpp}`,
  `vk_instance.{h,cpp}`, `vulkan_device.cpp`, `vulkan_device_surface.cpp`, `vulkan_device_buffers.cpp`, `tests/rhi` (`CMakeLists.txt`,
  `adapter_test.cpp`, `image_compare.cpp`, `types_test.cpp`, `vulkan_pipeline_test.cpp`, `vulkan_reset_golden_test.cpp`, `vulkan_compute_test.cpp`,
  `vulkan_loader_test.cpp`, `device_host_test.cpp`, `vulkan_commands_test.cpp` and the four files with Win32 cases), `tests/phases.txt`, the
  `module_deps_check` and `platform_neutral_check` changes. On Linux: `scripts/test-phase.sh --affected` with asan and tsan, both unity settings.

## Runs

Windows PC, after the review's fixes:

- `scripts/test-phase.sh --affected`: `ran 880 of 1775 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 77 s; the last
  full run took 13 s` (everything: the day changed `CMakeLists.txt` files and the phase map).
- `scripts/test-phase.sh --affected --flavor asan`: `ran 633 of 1775 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 48 s;
  the last full run took 13 s`.
- Full `ctest -j8`: MSVC `100% tests passed out of 1775`, `windows-clang-cl` `100% tests passed out of 1043`, non-unity MSVC 1774 of 1775: the
  known phase 2 flake `FramePool does not commit slab memory until blocks are used [asan]` (`KNOWN-ISSUES.md`, with the numbers measured today).
- The RHI suite alone (`ctest -L rhi`): `100% tests passed out of 289` (281 passed, 8 skipped: the Wayland and X11 window cases).
- Linux: not run; pending until the phase close (Day 174), `KNOWN-ISSUES.md`.

## Post-implementation code review

`/code-review high` on the day's diff: 5 findings, 2 fixed, 3 declined.

- **Fixed (found by the runs, recorded here):** `WaitIdle` could return with `CompletedFence()` one short on AMD's driver; it now waits on the
  timeline. Test: `Vulkan: after WaitIdle the completed fence is the last submitted one` (fails without the fix).
- **Fixed:** `vulkan_device.cpp` included the Xlib surface header without using it (the Windows build saw a Linux-only declaration); removed.
- **Declined (filed):** the reset golden test's resident-memory and descriptor drift checks measure nothing on Windows (no `/proc`); Day 161 brings
  the reset tests to Windows and gives them a measure.
- **Declined (filed):** the read-only cache directory case does not run on Windows, where the read-only attribute does not stop writes; an
  ACL-based variant is not built, as for the settings tests.
- **Declined:** `EndFrame`'s frame accounting reads the same timeline, so on this driver a frame may count as complete one frame later. Nothing
  is released early, only a frame late; not worth a wait in the frame loop.
