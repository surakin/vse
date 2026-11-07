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
| 0 .. G-1 | one CBV/SRV/UAV descriptor table per bind group, in group order | bind group layouts |
| G .. 2G-1 | one sampler descriptor table per bind group that has samplers | bind group layouts |
| last | root constants for push constants (up to 32 DWORDs) | `SetPushConstants` |

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
