# Phase 1 / Day 17 — GPU Driver Library Loading

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 2 (OS interaction wrappers).

## Tasks

- [x] Load `libvulkan.so` via the OS dynamic loader (`dlopen`, libc).
- [x] Resolve `vkGetInstanceProcAddr` via `dlsym`.
- [x] Use the resolved `vkGetInstanceProcAddr` to fetch
  `vkCreateInstance`.
- [x] Log success/failure at each step (missing driver should fail
  clearly, not crash).
- [x] Commit.

**Done:** `vkGetInstanceProcAddr` resolves and successfully fetches
`vkCreateInstance`.

## Resolution

Added a generic `os::DynamicLibrary` primitive to `os.h`
(`LoadLibrary`/`GetSymbol`/`UnloadLibrary`, backed by
`dlopen`/`dlsym`/`dlclose` in `os/linux/os.cpp`) rather than hand-rolling
the loading logic inline for Vulkan specifically — the same
interface-header/per-platform-impl split as the rest of `os.h`, so a
later Windows/macOS backend plugs in against `LoadLibrary` without
changing any caller. `os::LoadLibrary` loads `libvulkan.so.1`
specifically (the versioned name the loader package always installs),
not the unversioned `libvulkan.so` symlink (only guaranteed alongside
the `-dev` headers).

**Filenames are UTF-8, not `const char*` (design correction made before
writing any code):** `os::LoadLibrary` takes `StringView`, not
`const char*`. This exposed that `os::Open` and `os::ReadFileToArena`
already had the same bug, both retrofitted to `StringView` in this same
pass. Since `StringView` isn't guaranteed null-terminated (it can be an
arbitrary substring) but the underlying POSIX calls require a C string,
each Linux impl function copies into a fixed `kMaxPathBytes` (4096,
matches Linux `PATH_MAX`) stack buffer and null-terminates before
calling `dlopen`/`open` — returning failure rather than truncating if the
path doesn't fit. `StringView`'s existing implicit `const char*`
constructor means every prior string-literal call site kept compiling
unchanged.

**Vulkan types:** the 3 declarations Day 17 needs
(`VkInstance`/`PFN_vkVoidFunction`/`PFN_vkGetInstanceProcAddr`) come from
`#include <vulkan/vulkan_core.h>` (the system `vulkan-headers` package),
not hand-declared — decided this way specifically to keep Day 17 minimal
and defer the real "vendor Vulkan-Headers vs. rely on system headers"
call to Phase 3, which needs the full API surface and deserves its own
deliberate decision (the same weight FreeType's vendoring got).

The actual smoke-test logic (`os::ProbeVulkanCreateInstance`,
`engine/foundation/os/gpu_driver_probe.h`/`.cpp`) is a thin consumer of
`DynamicLibrary`: loads `libvulkan.so.1`, resolves
`vkGetInstanceProcAddr`, calls it with `VK_NULL_HANDLE` to resolve the
global-level `vkCreateInstance` entry point, logging success/failure at
each step through a new `LogPlatform` channel
(`log/log.h`/`log/log.cpp`) rather than asserting/crashing — a missing
GPU driver is an expected, recoverable runtime condition. Not wired into
`main.cpp` (matching Day 15/16, which also added no `main.cpp` code) —
fully exercised by 5 new tests in
`tests/foundation/gpu_driver_loading_test.cpp`, including one that
directly asserts the Done condition
(`ProbeVulkanCreateInstance() != nullptr`). All 72 tests green (was 67),
verified with `engine_main_asan` running clean under ASan/UBSan.
