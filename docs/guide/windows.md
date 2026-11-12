# Windows

Status: written (Phase 13; input, clipboard and drag and drop from Phase 15 Day 205).

**Audience:** anyone who builds, runs or debugs the engine on Windows x64. Read [Getting started](getting-started.md) and
[Building and testing](building-and-testing.md) first: everything in the other guides applies, and this page is only what is different on Windows.

Windows runs the same code as Linux with two graphics backends (Vulkan and Direct3D 12). The build finds the compilers by itself, so the most common
mistake is starting from the wrong shell.

## At a glance

| I want to | Do this |
|---|---|
| build | `cmake --preset windows-msvc` then `cmake --build --preset windows-msvc`, from plain PowerShell or Git Bash |
| check the second compiler | the `windows-clang-cl` preset |
| run tests | `scripts/test-phase.sh --affected` from Git Bash (plain and asan; no ThreadSanitizer) |
| pick the graphics API | `--rhi vulkan\|dx12` at start (not changeable while running) |
| turn on GPU validation | `--validation` (needs "Graphics Tools" for DX12) |
| read a crash | `scripts/crash-report.ps1 <envelope> <program> <symbol store>` |
| find user files | `%APPDATA%\<app>` (settings), `%LOCALAPPDATA%\<app>` (cache, state, crashes) |

## Source of truth

- `BUILD.md` ("Windows"): the prerequisites, each with the version in use and how to install it.
- `cmake/toolchains/windows-msvc.cmake`, `windows-clang-cl.cmake`, `CMakePresets.json`.
- `docs/superpowers/specs/2026-11-07-windows-port-design.md`: the decisions of the port, the DX12 backend's model, the measurements.
- `plans/phase-13-windows-dx12.md`.

## Prerequisites

Install what `BUILD.md` lists as required: Visual Studio Build Tools 2026 with the C++ workload and its AddressSanitizer component (MSVC 14.51,
Windows SDK 10.0.26100), CMake 3.21 or newer, Ninja on `PATH`, Git for Windows (its `bash` runs the test script), a GPU driver with Direct3D 12
(feature level 12_0) and Vulkan 1.3, and the Vulkan SDK 1.4 (its headers and validation layers). For the checks: LLVM 23 (`clang-cl`), the
"Graphics Tools" optional feature (the D3D12 debug layer behind `--validation`), and `slangc` exactly v2026.18.2 if you change shaders.

## How the build finds the compilers

The toolchain files find Visual Studio (through `vswhere`), the Windows SDK (from the registry), `clang-cl` (in `C:\Program Files\LLVM`) and the
Vulkan SDK by themselves. **Configure from a plain PowerShell or Git Bash, never from a Developer Command Prompt** or after `vcvars*.bat`: a prompt
that names a different MSVC stops the configure. For an unusual install, set any of these as a cache or environment variable:

| Variable | Names |
|---|---|
| `ENGINE_VS_INSTALL_DIR` | the Visual Studio or Build Tools installation |
| `ENGINE_WINDOWS_SDK_DIR`, `ENGINE_WINDOWS_SDK_VERSION` | the Windows SDK root and version |
| `ENGINE_LLVM_DIR` | the LLVM installation (`clang-cl`, `lld-link`) |
| `ENGINE_VULKAN_SDK` | the Vulkan SDK (else `VULKAN_SDK`, else the newest `C:\VulkanSDK\<version>`) |
| `ENGINE_SLANGC` | `slangc` (else `PATH`, else `C:\Tools\slang-2026.18.2\bin`); without it the committed minted shaders are used |
| `ENGINE_DXC` | a `dxc` or its directory, for DXIL minting (else the Windows SDK's) |
| `ENGINE_NINJA` | Ninja, if it is not on `PATH` |

## Building

Two configurations, both kept building every day (warnings are errors in both):

```
cmake --preset windows-msvc          # build-msvc: MSVC, the main one, with the asan flavor
cmake --build --preset windows-msvc
cmake --preset windows-clang-cl      # build-clang-cl: the second compiler
cmake --build --preset windows-clang-cl
```

The unity build is on by default; `-DENGINE_UNITY_BUILD=OFF` (the trees `build-windows-msvc-nounity`, `build-windows-clang-cl-nounity` by
convention) must build clean too. A Release tree is `cmake -S . -B build-msvc-release -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/windows-msvc.cmake
-DCMAKE_BUILD_TYPE=Release`; every program still gets its PDB beside it.

Windowed programs (`rhi_demo`, `renderer_demo`, `hello_window`) use the GUI subsystem through `ENGINE_WINDOWED_PROGRAM` in their source; their
log still reaches a console they were started from. Tests and tools are console programs.

## Running tests

From Git Bash, as on Linux (`docs/guide/building-and-testing.md`, "Test phases and scoped runs"):

```
scripts/test-phase.sh --affected                  # build-msvc by default on Windows
scripts/test-phase.sh --affected --flavor asan
scripts/test-phase.sh --all --flavor all          # plain and asan: there is no ThreadSanitizer on Windows
scripts/test-phase.sh --all --build-dir build-clang-cl
ctest --test-dir build-msvc -L docs
```

The GPU tests run once per backend (`FOR_EACH_GPU_BACKEND` in `tests/rhi/backend_fixture.h`): Vulkan and Direct3D 12 here. Set
`ENGINE_RHI_CACHE_DIR` to a scratch directory when you run `rhi_tests.exe` or `renderer_tests.exe` by hand, so the pipeline cache does not land in
your profile. The demo tests use a profile of their own in the build tree (`APPDATA` and `LOCALAPPDATA` point to `test-tmp/profile`; every test's working files are
under `test-tmp/`, see `docs/guide/building-and-testing.md`, "Where tests keep their files"). A test binary that hits an
assertion shows the C runtime's "abort() has been called" box and waits (`KNOWN-ISSUES.md`); under ctest that costs the test's timeout.

## Running the demos

```
build-msvc/game/renderer_demo/renderer_demo.exe                      # Direct3D 12 (auto)
build-msvc/game/renderer_demo/renderer_demo.exe --rhi vulkan --validation
build-msvc/game/rhi_demo/rhi_demo.exe --list-gpus --rhi dx12         # the hardware adapters and WARP
```

`--rhi` (the `renderer.backend` setting) chooses the graphics API when the program starts: `auto` is Direct3D 12 on Windows. It does not change
while the program runs; a change waits for the next start. `--gpu` (`renderer.adapter`) chooses the GPU and can change at run time, WARP (`Basic
Render`) included. The window's size is in logical pixels, scaled by the display's DPI. The benchmarks take `--rhi` as a launch option.

## Validation and the debug layer

`--validation` (`renderer.validation`, `renderer.sync_validation`) turns on the Vulkan validation layers with synchronization validation, or the
D3D12 debug layer with GPU-based validation. D3D12 errors and warnings reach the `LogRhi` log as `D3D12 debug layer [id]: ...`; the GPU tests fail
on any (`BackendValidation`). The debug layer needs the "Graphics Tools" optional feature; without it `--validation` on DX12 is refused with that
reason. PIX is the tool for GPU captures of the DX12 backend.

## Crash dumps and symbols

The crash handler (sentry-native with Breakpad, no upload) is on by default in Debug builds and off in Release (`--crash-handler=on|off`). A crash
leaves an envelope with the minidump and the engine's crash report in `%LOCALAPPDATA%\<app>\crashes`. Then:

```
scripts/store-symbols.ps1 build-msvc-release/game/renderer_demo/renderer_demo.exe symbols    # file the PDB and the image under the build id
scripts/find-dump.ps1 renderer_demo                                                           # list the envelopes
scripts/crash-report.ps1 <envelope> build-msvc-release/game/renderer_demo/renderer_demo.exe symbols
scripts/open-dump.ps1 <envelope> symbols                                                      # WinDbg or cdb, if installed
```

`crash-report.ps1` names every frame with its function, file and line from the store (DbgHelp, no network); it refuses a program whose build id
differs from the dump's. See `docs/guide/foundation-debugging.md` and `docs/guide/porting-debugging.md`.

## Input, the cursor, the clipboard and drag and drop

Since Phase 15 Day 205 the Win32 window gives the same input event stream as the Linux backends (`engine/platform/BACKEND.md`, the `windows` section): keys
with repeat, text from `WM_CHAR` (dead keys and AltGr are the system's; no IME composition), the pointer, the wheel in notches, focus and resize. The cursor shapes
are the system's; a custom image keeps its pixel size. The pointer lock is `ClipCursor` plus raw mouse input. The clipboard is `CF_UNICODETEXT`, `CF_HDROP` and
registered custom formats, with no primary selection, and file lists keep only local drive paths (`os::win32::AcceptLocalPath`). Drag and drop is OLE; a drag
out of the window is modal (`BeginDrag` returns when it ends), so keep a modal-loop callback set if the program must keep drawing meanwhile.

## Where things live

| What | Where |
|---|---|
| The user's settings | `%APPDATA%\<app>\settings.cfg`, the same file and format as on Linux (`--save-settings` writes it); a program may keep them in the registry instead (`settings::RegistryConfigStore`, `HKCU\Software\<app>`) |
| Policies (read-only, above everything) | `HKCU` and `HKLM` `Software\Policies\<app>` (`docs/guide/settings.md`, "Settings on Windows") |
| Configuration files | `%APPDATA%\<app>` |
| Data, cache and state | `%LOCALAPPDATA%\<app>` (`cache`, `state`) |
| The pipeline cache | `%LOCALAPPDATA%\<app>\cache`: Vulkan's `pipeline_cache_<vendor>_<device>_<uuid>.bin`, DX12's `pipelines_<adapter id>_<driver>.bin` |
| Crash dumps | `%LOCALAPPDATA%\<app>\crashes` |
| The socket backend | `engine/net/windows/` (Winsock 2, Phase 20 Day 278; `docs/guide/net.md`, "On Windows") |

Paths are UTF-16 at the system boundary and UTF-8 everywhere in the engine; the file layer adds the `\\?\` prefix past 248 characters, so long
paths work without the system's long-path setting (`docs/guide/io.md`).

## Common mistakes

- **Configuring from a Developer Command Prompt** or after `vcvars*.bat`: the configure stops if that prompt names a different MSVC. Use plain PowerShell or Git Bash.
- **Reading a crash without the symbol store of that build.** File the PDB with `store-symbols.ps1` for every build you ship; `crash-report.ps1` refuses a different build.
- **Expecting a run-time switch of the graphics API.** `--rhi` is read at start; only `--gpu` can change while running.
- **Running GPU tests by hand without `ENGINE_RHI_CACHE_DIR`**: the pipeline cache lands in your profile.
- **Expecting a report from a fast fail** (`__fastfail`, `/GS` checks): exit code `0xC0000409`, no report, no dump.
- **Expecting ThreadSanitizer.** MSVC has none; tsan runs on Linux.
- **Including `<winsock2.h>` directly.** Only `engine/net/windows/` calls Winsock, through `net/windows/winsock.h` (after `win32.h`); everything else uses
  `net::Socket`. The gates `net_access_check` and `platform_neutral_check` refuse it elsewhere.
- **Expecting a refused connect to be quick everywhere.** It is on loopback; to another host Windows retries for about two seconds first.

## See also

[Foundation: Debugging](foundation-debugging.md), [Porting the debugging facilities](porting-debugging.md), [Settings](settings.md), [io](io.md),
[RHI](rhi.md), [Platform: windowing and input](platform-windowing-and-input.md).

## Known gaps

`KNOWN-ISSUES.md` has the current list. The large ones: no enhanced barriers or placed resources in the DX12 backend; HDR10 is never granted (no
10-bit RHI format) and scRGB has not been seen on an HDR display; no exclusive fullscreen; no IME composition; x64 only (no ARM64); the DX12 draw call
costs about 20% more CPU than Vulkan's on the development PC; and a few interactive checks (dragging, real mouse input) have not been done by a person.
