# Windows Port + DX12 Backend — Design (Phase 13, Day 147)

Confirms or amends the twelve decisions of `plans/phase-13-windows-dx12.md` with the experiments run on the Windows PC on 2026-11-07, and records
the inventory of Linux-only code, the prerequisites, the DX12 object and root-signature model, and the rules the days follow. The phase file is the plan;
this file is the evidence. The RHI concept map this builds on is `2026-11-04-rhi-portability-review.md`, section 1.

## 1. Machines and prerequisites

**Windows PC** (`archvile-win`): AMD Ryzen AI 7 350 (8 cores, 16 threads), AMD Radeon 860M integrated GPU (D3D12 driver 32.0.32015.7029; Vulkan
driver "AMD proprietary driver 26.10.1 (LLPC)", Vulkan 1.4.359), 63 GiB RAM, Windows 11 Pro build 26300. D3D12 on this GPU (Spike B): highest shader model
6.8, resource binding tier 3, enhanced barriers supported.

| Prerequisite | State on 2026-11-07 | Version |
|---|---|---|
| Visual Studio Build Tools 2026 | installed | 18.10, MSVC 14.51.36231, with the C++ AddressSanitizer component |
| Windows SDK | installed | 10.0.26100.0; its `dxc` is 1.9.2602.17 |
| CMake, Ninja, Git for Windows | installed | 4.4.3, 1.13, 2.55.0 |
| LLVM (`clang-cl`, `lld-link`) | installed | 23.1.5, `C:\Program Files\LLVM`, not on `PATH` |
| Vulkan SDK | installed | 1.4.363.0, `C:\VulkanSDK\1.4.363.0`; ships a second `dxc` (1.9.0.5512, older than the SDK's) |
| `slangc` | installed | 2026.18.2, `C:\Tools\slang-2026.18.2\bin`, not on `PATH` (the build reads `ENGINE_SLANGC`) |
| WinDbg, PIX | installed | WinDbg (Store package), PIX for Windows |
| **Graphics Tools** (D3D12 debug layer) | **missing** (`d3d12SDKLayers.dll` absent) | needed from Day 164; Spike B ran without it |
| Debugging Tools for Windows (`cdb`) | not installed | optional: WinDbg covers the dump checks |
| Long paths (`LongPathsEnabled`) | off | Day 151 tests with it off |
| Developer Mode | off | symlink tests skip (Day 152) |

**Two `dxc` builds are on the machine.** The Vulkan SDK's installer puts its `Bin` (with an older `dxc`) on `PATH`; the Windows SDK's is newer. The build
uses the one Day 148's toolchain discovery finds in the Windows SDK (or `ENGINE_DXC`), never the first `dxc` on `PATH`; Day 163 records which produced the
committed DXIL.

**Linux machine:** the separate development machine with the NVIDIA RTX 3060 on which the golden images were blessed (Wayland and X11). Its CPU,
distribution, compiler and how it is reached from the Windows PC are recorded here by the developer (Day 147 task, not yet done on the Windows side).

**Two-machine procedure.** A day that changes shared code is committed and pushed from the machine it was written on; the other machine pulls, runs
`scripts/test-phase.sh --affected` (and the sanitizer flavors the day asks for), and both final lines are quoted in the day file and `STATUS.md`. Until Day
150 `test-phase.sh` does not run on Windows; the Windows lines of Days 148-149 are plain `ctest` results and say so.

## 2. The decisions

1. **Toolchain — confirmed** (Spike C). Every engine source outside the per-platform directories was compiled alone with
   `cl /std:c++20 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHs-c- /GR- /W4 /D_HAS_EXCEPTIONS=0 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DUNICODE`:
   54 of 97 compile as they are; with two portability macros (below) **92 of 97**. The five left are the expected platform files: libbacktrace's header
   (`stack_trace_backtrace.cpp`, Linux-only by decision 5), `sched.h`/`pthread.h` (`jobs/topology.cpp`, `jobs/worker_pool.cpp`, Day 153), the Xlib surface
   (`rhi/vulkan/vk_xlib_surface.cpp`, Linux-only source), and **`platform/key_translate.cpp`, a neutral file that includes `xkbcommon/xkbcommon-keysyms.h`**
   (section 3, item 3). No `windows.h` collisions showed because no shared file includes it yet; Day 148's wrapper test is where they appear.
   - The two causes of every other failure: the GCC `__attribute__((format(printf, ...)))` on the log helpers (`rhi_log.h`, `renderer_log.h`,
     `jobs_log.h`, `gbuffer.h`, `render_graph.h`, `renderer.h`, `shader_reflection.cpp`, `null_device.cpp`, `settings/report.cpp`,
     `tools/shader_mint/text.h`) and `__builtin_trap` in `jobs/job.h`. Day 148: an `ENGINE_PRINTF_FORMAT(fmt, args)` macro in `debug/compiler.h` (empty on
     MSVC, `_Printf_format_string_` on the format parameter where it helps) and the trap through `compiler.h`'s existing trap macro.
   - `/W4` warnings left after those fixes: C4996 (MSVC's deprecation of `strncpy` and `getenv`; the engine uses the C library on purpose, so
     `_CRT_SECURE_NO_WARNINGS` is defined for every target), C4649 (an attribute MSVC ignores in that position: `program_setup.cpp`, `io/file.cpp`,
     `vk_instance.cpp`, `vulkan_device_buffers.cpp`), C4324 (`jobs/deque.h` padded by its `alignas`, intended: disabled for that struct), C4189 (an unused
     variable in `vk_memory.cpp`, a real cleanup). Day 148 decides whether warnings are errors on Windows (Linux's rule is followed).
   - **`clang-cl` and `__int128`:** `clang-cl` has native `unsigned __int128`, but division and remainder call compiler-rt (`__udivti3`, `__umodti3`), which
     `clang-cl` does not link against the MSVC runtime: the link fails. Day 148 links `clang_rt.builtins-x86_64.lib` from the LLVM install it found
     (`lib/clang/<major>/lib/windows/`) in the `clang-cl` configuration, so the native path is the one tested there; the portable path stays MSVC's.
   - **The portable 128-bit path under MSVC is correct:** 200 000 random operations (`u128` `+ - * / % << >> <`, `i128` `/ % >> <`, shift counts 0-139,
     operand shapes from 16 to 128 bits) give the same results and checksum (`d872f86c1460ab89`) built with MSVC (portable) and with `clang-cl` (native
     `__int128`). This closes the Phase 10 known issue once Day 148 runs the real `tests/types` suite under MSVC.
2. **The C runtime is allowed — confirmed** (no experiment needed; `PLAN.md` assumption #2).
3. **Descriptors — decided: an engine table of `HANDLE`s behind the existing `int`** (Spike A). A thread blocked inside the UCRT's `_write` on a pipe and
   then suspended (as a dump writer suspends threads) holds the UCRT's per-descriptor lock. With the pipe drained by a reader, a crash handler's `_write`
   on the same descriptor still **blocked** (2 s timeout), while `WriteFile` on the same OS handle **completed**, and a newly created report file always
   completed. With the pipe full, both blocked, as any writer would. Cost is the same: 1.68 µs per 64-byte `_write` and 1.52 µs per `WriteFile` to a file.
   So `os::Open`/`CreateFileForWriting`/`Write`/`Read`/`Close` keep their `int` signatures and map them to an engine table of `HANDLE`s (0, 1 and 2 are the
   standard handles from `GetStdHandle`), written with `ReadFile`/`WriteFile`, with no lock the crash path could find held; the table's slot allocation is
   lock-free or done before the crash path needs it (Day 149 decides, `signal_safety_check` checks). The crash report keeps writing its own new file, and its
   copy to standard error stays best-effort with `WriteAllWithTimeout`.
4. **Manifest — confirmed as planned:** `activeCodePage` UTF-8, `longPathAware` true, `dpiAwareness` PerMonitorV2 (with `dpiAware` true/pm as the fallback),
   `supportedOS` GUIDs for Windows 10 and 11. Note: `longPathAware` has effect only where `LongPathsEnabled` is 1, which is not the case on this PC.
5. **PDBs and DbgHelp — confirmed** (no experiment). libbacktrace is not built on Windows.
6. **One Win32 window backend — confirmed** (no experiment).
7. **Vulkan on Windows first — confirmed:** the Vulkan SDK's `vulkaninfo` lists the Radeon 860M with Vulkan 1.4.359; the loader ships with the driver.
8. **Neutral RHI changes first (Day 162) — confirmed**, unchanged.
9. **The per-object index — AMENDED: a per-instance vertex stream, not a root constant** (Spike B). A D3D12 program with `slangc`-minted DXIL (both
   pipelines created, so the DXIL is signed) drew 64 objects whose index arrived (a) as a root constant set before each draw and (b) as an instance-rate
   vertex attribute read from a buffer `[0, N)`, with draw `i` issued with `StartInstanceLocation = i`. Both were correct (64 of 64 objects in their own
   color). 10 000 draws, median of 20: (a) 21.5 ns per draw to record, 0.491 ms record plus GPU; (b) **14.8 ns per draw, 0.420 ms**. (b) also needs no RHI
   or `GBufferPass` change: the renderer already draws object `i` with `first_instance = i`, and D3D12 fetches instance-rate data at
   `StartInstanceLocation + SV_InstanceID`. Day 163: the material vertex shaders read the index from an instance-rate `OBJECT_INDEX` attribute on the D3D12
   (and, if it unifies cleanly, every) target, fed by one static index buffer bound at vertex-buffer slot 1; `SV_StartInstanceLocation` (shader model 6.8,
   present on this GPU) is rejected because the baseline is 6.0. The define is `ENGINE_OBJECT_INDEX_STREAM`. **Not verified: the debug layer** (Graphics
   Tools missing); rerun the spike with `--debug` once it is installed and record the message count.
10. **D3D12 baseline — confirmed:** feature level 12_0 and shader model 6.0 as the floor (this GPU reports 6.8 and tier 3, which the backend may use only behind
    a capability check), resource binding tier 2 required, legacy barriers (enhanced barriers are supported here but stay out until measured), the OS's own
    `d3d12.dll`, no Agility SDK, no D3D12MA, no DirectX-Headers package, no WRL; `ComRef<T>` of our own.
11. **Golden images are not re-blessed — confirmed** (no experiment yet; Day 160 is the first comparison on this GPU).
12. **Line endings pinned — done today:** `.gitattributes` (`* text=auto eol=lf`; `*.ppm`, `*.rtex`, `*.envelope`, `*.dmp`, `*.bin`, `*.spv` binary;
    `tests/docgen/fixtures/good/crlf.h` and `third_party/**` kept byte for byte). Two files were stored with CRLF on purpose (the docgen CRLF fixture and
    sentry's `sentry.rc.in`), which is why they are exempt. Renormalizing changed no stored file. The Windows checkout's working-tree files still carry the CRLF
    that `core.autocrlf=true` wrote; they become LF when the checkout is refreshed (section 6).

## 3. Found while measuring (each becomes a task in the day named)

1. **The `clang-cl` builtins** (decision 1) — Day 148.
2. **`tests/debug` uses `-Wl,--wrap=malloc`** (`tests/debug/CMakeLists.txt`) to prove the crash path does not allocate. There is no `--wrap` in `link.exe` or
   `lld-link`; Day 155 picks the Windows form (hooking the engine's own heap entry points through a test-only seam, or counting with the CRT debug heap's
   allocation hook `_CrtSetAllocHook` in a Debug build) and records it.
3. **`platform/key_translate.cpp` is neutral but includes the XKB keysym header** (header-only constants). The Wayland and X11 backends translate XKB
   keysyms; Win32 translates scan codes. Day 159 moves the keysym translation to `platform/linux/` and leaves the neutral file the parts every backend
   shares. `module_deps_check` did not see it because system headers are not modules; Day 150 adds a rule that neutral files include no platform system
   header (`xkbcommon/`, `wayland-`, `X11/`, `windows.h`, `d3d12.h`, `vulkan_win32.h`, ...).
4. **The Vulkan SDK's older `dxc` on `PATH`** (section 1) — Day 163.
5. **The working-tree refresh** (decision 12) — section 6.

## 4. Inventory of Linux-only code

What is Linux-specific, its Windows counterpart, and the day that ports it. Files under a per-platform directory are listed by directory.

| Where | Linux-specific | Windows counterpart | Day |
|---|---|---|---|
| root `CMakeLists.txt` | stop on non-Linux; `-Wl,--build-id`; `ENGINE_SPLIT_DEBUG_INFO` with `split-debug.sh`; GCC production flags | supported-platform check; `/DEBUG:FULL`, `/Zi`; PDBs always; per-compiler options | 148 |
| `engine/foundation/CMakeLists.txt` | Linux source lists; `CMAKE_DL_LIBS`; `-fsanitize=...` on `foundation_base_asan`/`_tsan` | Windows source lists; `/fsanitize=address`; no TSan target | 148, 149 |
| `engine/platform/CMakeLists.txt` | Wayland, xkbcommon, X11 through pkg-config | user32, shcore (DPI) | 158 |
| `engine/foundation/os/linux/` (`os.cpp`, `command_line.cpp`, `session.cpp`) | syscalls, `environ`, `/proc`, logind | `os/windows/` per decision 3; `GetCommandLineW`; `SESSION.md` | 149 |
| `engine/foundation/io/linux/` (`local_file`, `file_system`, `directory`, `error`) | POSIX file API, `renameat2`, `O_TMPFILE`, XDG folders | `CreateFileW`, `ReplaceFileW`, `FindFirstFileExW`, known folders | 151, 152 |
| `engine/foundation/os/linux/debug/` (8 files) | signals, `fork`-free child, `/proc`, core dumps, `ptrace` checks | DbgHelp, `on_crash` hook, `CreateProcessW` + job object, WER | 154-156 |
| `engine/foundation/debug/stack_trace_backtrace.cpp` | libbacktrace | `stack_trace_dbghelp.cpp` | 154 |
| `engine/foundation/settings/` | none (the environment source went behind `os::` in Phase 12) | the registry store under `settings/windows/` | 157 |
| `engine/foundation/log/log.cpp` | `<fcntl.h>` | through `os::` (decision 3) | 149 |
| `engine/jobs/topology.cpp`, `worker_pool.cpp` | `sched_getaffinity`, `pthread_setaffinity_np`, `syscall(gettid)` | `GetLogicalProcessorInformationEx`, `SetThreadGroupAffinity`, `SetThreadDescription` | 153 |
| `engine/jobs/job.h` | `__builtin_trap` | `compiler.h` trap macro | 148 |
| log-helper headers (10 files, decision 1) | `__attribute__((format))` | `ENGINE_PRINTF_FORMAT` | 148 |
| `engine/foundation/debug/compiler.h`, `int128.h`, `types.h` | GCC builtins, `__int128` | MSVC intrinsics; portable path; `long` is 32-bit | 148 |
| `engine/platform/linux/` (Wayland, X11, xkb, window host) | display backends | `platform/windows/` Win32 window, input, host | 158, 159 |
| `engine/platform/key_translate.cpp` | XKB keysym header (section 3, item 3) | scan-code table in `platform/windows/` | 159 |
| `engine/rhi/vulkan/vk_loader.cpp`, `vk_xlib_surface.cpp`, `vulkan_device_surface.cpp` | `libvulkan.so.1`; Xlib and Wayland surfaces | `vulkan-1.dll`; `VK_KHR_win32_surface` | 160 |
| `engine/rhi/vulkan/create_device.cpp`, `device_host.cpp`; renderer links `rhi_vulkan` | the two allow-listed RHI edges | the backend registry | 162 |
| `shaders/` | SPIR-V only; `SV_VulkanInstanceID` | DXIL `*.dxil.inc`; instance stream (decision 9) | 163 |
| `tools/shader_mint/main.cpp` | `posix_spawnp` of `slangc` | process helper with `CreateProcessW` | 163 |
| `tools/docgen/src/` (`main.cpp`, `samples.cpp`, `symbol_index.cpp`) | `stat`, `posix_spawn`, `waitpid`, signals | the file layer and a process helper | 150 |
| `game/rhi_demo`, `game/renderer_demo`, `game/rhi_bench` (`main.cpp`) | `<unistd.h>` | through `os::` | 161 |
| `tests/debug/*_linux_test.cpp` (7 files), `resource_guard_linux.cpp` | Linux crash, core-dump and script tests | `*_windows_test.cpp` twins where behavior differs | 154-156 |
| `tests/debug/CMakeLists.txt` | `-Wl,--wrap=malloc` (section 3, item 2) | Windows allocation check | 155 |
| `tests/docgen/*_test.cpp` (7 files) | `dirent`, `stat`, `unistd` | test helpers through the file layer | 150 |
| `tests/io/temp_dir.h`, `directory_test`, `file_system_test`, `file_test` | raw POSIX shapes planted on purpose | Windows shapes (junctions, reparse points, ACLs) | 151, 152 |
| `tests/jobs/async_io_test.cpp`, `frame_pool_test.cpp`; `tests/rhi/image_compare.cpp`, `vulkan_pipeline_test.cpp`, `vulkan_reset_golden_test.cpp`; `tests/settings/file_store_test.cpp`; `tests/foundation/log_test.cpp`, `window_backend_test.cpp` | `unistd`, `fcntl`, `dirent`, `sys/socket.h`, Linux environment variables (`WAYLAND_DISPLAY`, `DISPLAY`, `TMPDIR`, `XDG_*`, `HOME`) | the file layer, `os::`, platform-qualified test files | 149-161, by suite |
| `scripts/` (`test-phase.sh`, `crash-report.sh`, `find-core.sh`, `open-core.sh`, `split-debug.sh`, `store-symbols.sh`) | bash, Linux tools | `test-phase.sh` under Git Bash (Day 150); PowerShell twins for the crash tooling (Day 156) | 150, 156 |
| `tests/*/cross_syntax/` checks | run `clang++ --target=` from Linux | keep (they guard the header-only check on Linux) | — |

## 5. The DX12 backend's model

**Objects.** The concept map (`2026-11-04-rhi-portability-review.md`, section 1) holds for every row; the DX12 column is the plan. Changes since: the
backend registry (Day 162) replaces "CreateDevice(Backend)"; the object index is an instance stream (decision 9); push constants are root constants.

**Root signature per pipeline layout:**

| Root parameter | Contents | Source |
|---|---|---|
| per group, in group order | its CBV/SRV/UAV descriptor table (if it has views), then its sampler table (if it has samplers) | bind group layouts |
| last | root constants for push constants (up to 32 DWORDs) | `SetPushConstants` |

(Amended on Day 167 to what Day 166 built: the tables of each group sit together and an empty table is left out, so the indices come
from `Dx12Device::RootLayout`, not from a formula.)

Root signature version 1.1 with the descriptor ranges marked `DATA_STATIC_WHILE_SET_AT_EXECUTE`; register spaces follow the bind group index (`space<g>`),
registers follow the binding (Slang assigns them from `[[vk::binding(b, g)]]`; Day 163 checks the minted reflection agrees). The object index uses no root
parameter (decision 9). Bind groups copy their CPU descriptors into the frame's shader-visible ring at `SetBindGroup` (Day 166 measures it).

## 6. Rules for the days

- **The working-tree refresh (developer action).** After `.gitattributes`, git reports the Windows checkout's files as modified because their on-disk copies
  still have CRLF; their stored content is unchanged. Refresh once with `git rm --cached -r -q . && git reset -q --hard` from a clean tree (or a fresh clone).
  Until then, commit only named files, never `git add -A`.
- **Test labels:** a shared test keeps its phase when it starts running on Windows; a new Windows-only test is phase 13; the platform-qualified `flavors` line
  arrives on Day 148 (moved from Day 150).
- **Symbols and dumps:** PDBs beside every program; the store layout `<name>.pdb/<GUID+age>/<name>.pdb` and `<name>.exe/<timestamp+size>/<name>.exe`
  (Day 156); dumps under `%LOCALAPPDATA%/<app>/crashes`.

## 7. Open after Day 147

- The Linux machine's details and access (section 1).
- The debug-layer run of Spike B, once Graphics Tools is installed.
- The Phase 13 day files' changes from this spec (decision 9 in Days 163, 166, 168, 171; section 3 items in Days 148, 150, 155, 159, 163) are made in the
  same commit as this spec.

## 8. The Vulkan baselines on the Windows PC (Day 161)

What Days 166, 171 and 173 compare the DX12 backend with, on the same machine. **Machine:** the Windows PC of section 1 (AMD Radeon 860M,
Vulkan driver 26.10.1), reached over **Remote Desktop**: the session's display is the Remote Display Adapter at 32 Hz, so a vsync-paced
frame takes 31.25 ms there (with or without the validation layer). The benchmarks render offscreen and are not paced. **Build:**
`build-msvc-release` (the MSVC toolchain file, `CMAKE_BUILD_TYPE=Release`; `cmake -S . -B build-msvc-release -G Ninja
-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/windows-msvc.cmake -DCMAKE_BUILD_TYPE=Release`), validation off. Two runs each; the table gives
both.

**`rhi_bench`** (512x512 offscreen target, 30 frames per draw measurement). Every draw of the cube benchmark creates a transient bind group
for its uniform slice, sets the pipeline, the viewport and scissor, the bind group and the vertex and index buffers, and draws
(`CubeRenderer::DrawWith`), so the per-draw figure is `CreateBindGroup` plus `SetBindGroup` plus a draw with its state:

| Measure | Run 1 | Run 2 |
|---|---|---|
| 1000 draws: record / submit / wall per frame | 0.822 / 0.027 / 0.878 ms (0.82 us per draw) | 0.734 / 0.024 / 0.788 ms (0.73 us per draw) |
| 10000 draws: record / submit / wall per frame | 8.704 / 0.034 / 9.232 ms (0.87 us per draw) | 7.858 / 0.028 / 8.376 ms (0.79 us per draw) |
| Buffer barrier / texture barrier, per call | 0.175 / 0.183 us | 0.143 / 0.164 us |
| Upload through the staging ring: 1 MiB x 256 / 64 MiB x 8 | 16245 / 14094 MiB/s | 16682 / 14447 MiB/s |
| Cube pipeline: no cache / cold cache / warm cache | 18.8 / 22.6 / 21.6 ms | 21.7 / 21.7 / 22.6 ms |

**`renderer_bench`** (1280x720, 300 point lights, the device idled outside the timer, so these are CPU times). The scene draws its objects
instanced: 16 draw calls whatever the object count, so the per-object figure is the instanced-draw cost per instance:

| Measure | Run 1 | Run 2 |
|---|---|---|
| Graph of 1000 passes: build / compile | 26.0 / 25.7 us | 22.8 / 22.5 us |
| Frame, 1000 objects (932 drawn, 16 draws): single thread / jobs, median | 0.247 / 0.442 ms | 0.234 / 0.406 ms |
| Frame, 10000 objects (9312 drawn, 16 draws) | 1.282 / 1.782 ms | 1.208 / 1.716 ms |
| Frame, 50000 objects (46374 drawn, 16 draws) | 6.729 / 7.337 ms | 6.749 / 7.536 ms |
| Lights: 1000 objects with 0 / 1000 / 16000 point lights | 0.126 / 0.323 / 3.323 ms | 0.129 / 0.311 / 3.299 ms |
| Transient pool after 105 frames | 5 created, 520 reused (99.0%) | the same |

**The demo scene on the GPU:** `renderer_demo --no-vsync --frames 4000` (Release, 1280x720 window, 540 objects of which 487 are drawn in 16
draws, about 298 point lights in volume draws): 1.81 to 1.90 ms per frame over two runs, about 540 frames per second.

**A device reset** (`renderer_demo --script` with 20 `reset` words, Debug build, validation off, `--no-vsync`): 238.9 ms median, 257.2 ms worst
from the apply call; the first good frame 243.8 / 262.4 ms. Split (medians): owners releasing 0.0, destroying the old device 70.4, creating the
device 56.3, the swapchain 53.5, owners rebuilding 56.1 ms. The same GPU under Linux (RADV, Phase 8 Day 113) took 36 ms (5.9, 9.7, 1.0, 12.3 for
the same steps): every step is slower here, the swapchain most (54 against 1 ms). A reset still needs no progress display (Phase 8's decision
asks again at a second or more).

## 9. DX12 beside Vulkan on the Windows PC, and beside Linux (Day 173)

The same machine and build as section 8 (`build-msvc-release`, validation off, two runs each), on 2026-11-08 at the PC itself, not over Remote
Desktop: its display runs at 120 Hz with 200% scaling, so the demo's default 1280x720 window is 2560x1440 pixels. Each program picks the backend
with `--rhi`. Linux is the other development machine (NVIDIA RTX 3060, Ryzen 9 7950X3D; Phase 4 Day 67 and the renderer guide's numbers of 2026-11-05), so its column
compares machines as well as systems.

**`rhi_bench`** (the per-draw figure is a transient bind group's `CreateBindGroup` plus `SetBindGroup` plus the draw with its state):

| Measure | DX12, run 1 / run 2 | Vulkan (Windows), run 1 / run 2 | Linux (Vulkan, RTX 3060) |
|---|---|---|---|
| 1000 draws: record per frame (per draw) | 0.441 / 0.394 ms (0.44 / 0.39 us) | 0.728 / 0.863 ms (0.73 / 0.86 us) | 0.50-0.52 ms (0.5 us) |
| 10000 draws: record per frame (per draw) | 4.515 / 4.112 ms (0.45 / 0.41 us) | 10.672 / 12.955 ms (1.07 / 1.30 us) | 6.2-6.4 ms (0.63 us) |
| Buffer / texture barrier, per call | 0.198 / 0.176, 0.214 / 0.199 us | 0.156 / 0.239, 0.181 / 0.197 us | 0.15-0.17, 0.17-0.21 us |
| Upload through the staging ring: 1 MiB x 256 / 64 MiB x 8 | 12980 / 8145, 16733 / 16682 MiB/s | 15465 / 18023, 14617 / 14368 MiB/s | 15.6-18.1 / 13.7-18.6 GiB/s |
| Cube pipeline: no cache / cold / warm | 1.29-1.40 ms, all three | 20.7-22.1 ms, all three | 5.6-5.8 / 4.9-5.0 / 3.6 ms |

**`renderer_bench`** (CPU time of a whole frame; 16 instanced draws whatever the object count):

| Measure | DX12, run 1 / run 2 | Vulkan (Windows), run 1 / run 2 | Linux (Vulkan, RTX 3060) |
|---|---|---|---|
| Graph of 1000 passes: build / compile | 25.9 / 22.7, 25.8 / 22.5 us | 26.0 / 22.6, 25.7 / 22.5 us | about 27 / 27 us |
| Frame, 1000 objects: single thread / jobs, median | 0.172 / 0.157, 0.204 / 0.173 ms | 0.262 / 0.173, 0.680 / 0.354 ms | 0.11 / 0.15 ms |
| Frame, 10000 objects | 2.018 / 2.010, 1.998 / 1.947 ms | 1.354 / 1.184, 1.588 / 1.506 ms | 0.78 / 0.8 ms |
| Frame, 50000 objects | 7.184 / 7.088, 7.783 / 7.391 ms | 6.260 / 6.263, 6.994 / 7.016 ms | 3.9 / 3.9 ms |
| Lights: 1000 objects with 0 / 1000 / 16000 point lights | 0.183 / 0.564 / 6.331, 0.171 / 0.848 / 6.325 ms | 0.200 / 0.507 / 5.154, 0.126 / 0.549 / 5.127 ms | +0.15 / +3.1 ms over none |
| Transient pool after 105 frames | 5 created, 520 reused (99.0%) | the same | 5 created, 520 reused |

**The jobs benchmarks** (`jobs_tests "[throughput],[latency]"`, Release; backend-independent): 3000 CPU-bound jobs of about 60 us on 1, 2, 4 and 16
participants: 0.180, 0.081, 0.050, 0.013 s (speedup 1.00, 2.21, 3.61, 13.80 on 8 cores and 16 threads); the wake-up of an idle worker: median
85.7 us, 99th percentile 210.2 us, worst 2091 us over 10000 wake-ups.

**The demo scene on the GPU** (`renderer_demo --no-vsync --frames 4000`, Release, 2560x1440 swapchain, 487 objects in 16 draws, about 298 point
lights): DX12 6.36 to 6.87 ms a frame, Vulkan 6.04 to 6.19 ms (two runs each, the last three one-second averages of each). Section 8's 1.81 to
1.90 ms was a 1280x720 swapchain over Remote Desktop: a quarter of the pixels. There is no GPU timestamp in the demo, so these are frame times of a
GPU-bound loop. With `--set renderer.width=640 --set renderer.height=360` DX12 renders 640x360 buffers that DXGI scales to the window (0.60 to
0.69 ms), while Vulkan keeps the window's size (the Win32 surface reports a fixed current extent, so the requested size is not used): the
`SwapchainDesc` size is a request, and the two backends answer it differently on Windows (`KNOWN-ISSUES.md`).

**A device reset** (`renderer_demo --script` with 20 `reset` words, Debug, validation off, `--no-vsync`, 2560x1440): DX12 266 ms median (261 to
309), of which creating the D3D12 device takes 189 to 206 ms; Vulkan 284 ms median (265 to 302), against section 8's 239 ms at a quarter of the
pixels.

**The Phase 4 prediction checked.** The portability review (`2026-11-04-rhi-portability-review.md`, "Bind groups and descriptor heaps") predicted
that D3D12's one shader-visible heap would cost a descriptor copy at every `SetBindGroup`, and the Day 67 baseline recorded Vulkan's cost of the same
call for this comparison. Measured: the copy is not a cost. A draw with its transient bind group records in 0.39 to 0.45 us on DX12 against 0.73
to 1.30 us on Vulkan on the same machine (and 0.5 to 0.63 us on the Linux machine's Vulkan): `CopyDescriptorsSimple` into the frame's ring is
cheaper than allocating and writing a Vulkan descriptor set. Where DX12 is slower is the bare draw call: the renderer's light-volume loop sets its
groups once and then only draws, and there DX12 takes about 0.38 us a light against Vulkan's 0.31 us (the 16000-light rows), so a frame of many
small draws costs DX12 about 20% more CPU here, and the 10000- and 50000-object frames 50 to 70% and 13% more. Pipeline creation is the other large
difference: 1.3 ms on DX12 against 21 ms on Vulkan, and neither backend's own cache changes it on this driver (both drivers keep a cache of their
own).
