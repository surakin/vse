# BUILD.md — Build prerequisites per platform

What each platform needs installed to build the engine, run its tests and work on it. Build and test commands are in `STATUS.md` and
`docs/guide/building-and-testing.md`; this file lists only what has to be on the machine. Keep it current: a change that adds, removes or
re-versions a prerequisite updates this file in the same commit (`CLAUDE.md`).

Each tool is one of:

- **Required:** the build does not configure or link without it.
- **For checks:** the build works without it, but a project rule needs it (validation layers silent, the `clang-cl` build, minting shaders), or tests skip
  with a printed reason.
- **Optional:** a convenience for debugging or regenerating vendored files.

Everything third-party that ships inside the engine or a tool (Catch2, libbacktrace, sentry-native with Breakpad, nlohmann/json, yyjson 0.13.0 (Phase 22 Day 335, `third_party/yyjson/VENDORED.md`: the glTF importer's JSON), FreeType (Phase 14), libogg 1.3.6 and libvorbis 1.3.7 (Phase 26 Day 432, decode-only, `third_party/libogg/VENDORED.md`, `third_party/libvorbis/VENDORED.md`; used only by `engine/audio/vorbis.cpp`), SQLite 3.54.1 (Phase 19, the amalgamation, `third_party/sqlite/VENDORED.md`) and LuaJIT 2.1 (Phase 27 Day 445, the pinned commit in `third_party/luajit/VENDORED.md`; built by CMake with its own host tools, so it needs only the C compiler and, on Linux and macOS (Day 474), the assembler of the C compiler driver (on Windows, from Phase 27 Day 473, its VM is a COFF object that `buildvm` writes itself: MSVC or `clang-cl`, nothing more); used only by `engine/script/luajit/`; `-DENGINE_LUAJIT_JIT=OFF` (preset `linux-nojit`) builds it without the JIT compiler, for platforms that forbid run-time code generation)) is vendored under `third_party/` and built from source. **Do not install it separately.**

## Linux (x86-64): supported

The reference platform. Wayland is the primary display backend and X11 the fallback; both are needed for the full test suite. Package names below are
Arch Linux's, the distribution the development machine uses. `docs/guide/linux.md` is the guide.

| What | Version used | Kind | Notes | Arch package |
|---|---|---|---|---|
| CMake | 3.21 or newer (4.5.0 used) | Required | 3.21 for `CMakePresets.json` with toolchain files. | `cmake` |
| Clang (`clang++`) | C++20 (23.1.5 used) | Required | The compiler CMake picks first on Linux (GCC is the fallback when no `clang++` is installed; it must build clean too). Also runs the cross-target syntax checks (`*_cross_syntax_windows`, `_macos`). | `clang` |
| Ninja | 1.13 | Required | The generator the `linux` and `linux-nojit` presets use (2026-11-20). Make still works for a tree configured without a preset, but Ninja rebuilds faster: after touching `engine/renderer/gbuffer.h`, the plain flavor's target took 12.1 s against Make's 20.7 s, and a no-op 0.05 s against 0.45 s (`-j32`, 2026-11-19). | `ninja` |
| `sccache` | 0.x | Optional | Set as `CMAKE_C_COMPILER_LAUNCHER` and `CMAKE_CXX_COMPILER_LAUNCHER`. A clean build of a new tree with a warm local cache took 10.4 s against 87 s (Ninja, `-j32`, 2026-11-19). It does not speed up a rebuild after an edit, and with a cold cache it costs nothing. Never required. | `sccache` |
| `clang-format` | 18 or newer (23.1.5 used) | Required to commit | Formats the code (`scripts/format.sh`, `.clang-format`) and runs in the pre-commit hook (`scripts/install-git-hooks.sh`); 18 is the first version with `.clang-format-ignore`. | `clang` |
| `clang-tidy`, `run-clang-tidy` | 18 or newer (23.1.5 used) | For checks | `scripts/tidy.sh` (`.clang-tidy`). | `clang` |
| pkg-config | any | Required | Finds the system libraries below. | `pkgconf` |
| `libwayland-client` (headers) | any current | Required | Wayland backend. | `wayland` |
| `libxkbcommon` (headers) | any current | Required | Keyboard maps for Wayland. | `libxkbcommon` |
| `libX11` (headers) | any current | Required | X11 fallback backend. | `libx11` |
| `libXcursor`, `libXi`, `libXfixes` (headers) | 1.2.3, 1.8.4, 6.0.2 | Required | X11 backend: cursor themes and custom cursor images; XInput 2 raw motion for the pointer lock; selection-owner notifications for the clipboard. | `libxcursor`, `libxi`, `libxfixes` |
| Vulkan loader (`libvulkan.so.1`) and a Vulkan 1.3 GPU driver | | Required to run | Loaded at run time; the build does not link it. | `vulkan-icd-loader`, plus the driver |
| `bash` | any | Required | `scripts/test-phase.sh` and the scoped-test tooling. | `bash` |
| Vulkan validation layers | | For checks | `--validation` and the validation-silent checks. | `vulkan-validation-layers` |
| `slangc` | **exactly v2026.18.2** | For checks | Re-mints shaders. Without it the build uses the committed outputs. A different version produces different bytes, and the minted-output checks fail. | from the Slang GitHub release |
| `spirv-val` | any current | For checks | Validates minted SPIR-V when `slangc` is present. | `spirv-tools` |
| `dxc` | | Optional | DXIL step of `shaders_portability`. The Arch package (1.10.2605.37) aborts on these shaders (`KNOWN-ISSUES.md`); Windows' SDK `dxc` is the one that runs. | `directx-shader-compiler` |
| `lldb`, `gdb` | any current | For checks | Debugging tests that open dumps and cores; they skip without them. | `lldb`, `gdb` |
| `objcopy`, `strip`, `readelf` | any current | For checks | Split debug info, the crash-script tests, the no-network check. | `binutils` |
| `systemd-coredump` | | Optional | Where the system stores core dumps (`scripts/open-core.sh`). | part of `systemd` |
| Python 3 | 3.x | Optional | `scripts/list-aborts.py` only. | `python` |
| `pipewire` headers (`pipewire/pipewire.h`, `spa/param/audio/format-utils.h`) | 1.6.11 | Optional | The PipeWire audio backend (Phase 26 Day 425); the library itself is loaded at run time, never linked. Without the headers the backend reports itself absent and the ALSA backend (raw system calls) is used. | `pipewire` (headers included) |
| `wayland-scanner`, `wayland-protocols` | 1.26.0, 1.50 | Optional | Only to regenerate the vendored protocol code (`engine/platform/linux/wayland/generated/VENDORED.md`). | `wayland`, `wayland-protocols` |
| A Wayland compositor and an X server (or Xwayland) | | For checks | The `display` test group; it skips on a locked session. | |

## Windows (x86-64): supported (Phase 13)

**Status (2026-11-08, Phase 13 Day 174): every target builds and every test runs on Windows x64**, with MSVC (plain and ASan) and `clang-cl`
(plain), unity on and off, and the Vulkan and Direct3D 12 backends. The developer PC has the versions shown; `docs/guide/windows.md` is the
Windows guide.

The toolchain files (`cmake/toolchains/windows-msvc.cmake`, `windows-clang-cl.cmake`) find Visual Studio (through `vswhere`), the Windows SDK
(registry), `clang-cl` and the Vulkan SDK by themselves. **Configure from a plain PowerShell or Git Bash, never from a Developer Command Prompt or
after `vcvars*.bat`** (a Developer Command Prompt that names a different MSVC stops the configure); the `windows_toolchain_discovery_*` tests prove
it from a stripped environment:

```
cmake --preset windows-msvc          # or windows-clang-cl
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

The override variables for unusual installs (cache variables or environment variables) are `ENGINE_VS_INSTALL_DIR`, `ENGINE_WINDOWS_SDK_DIR`,
`ENGINE_WINDOWS_SDK_VERSION`, `ENGINE_LLVM_DIR`, `ENGINE_VULKAN_SDK` (then `VULKAN_SDK`, then the newest `C:\VulkanSDK\<version>`), and, from
Day 163, `ENGINE_SLANGC` (`slangc`; else `PATH`, then `C:\Tools\slang-2026.18.2\bin`) and `ENGINE_DXC` (a `dxc` or its directory, whose
`dxcompiler.dll` and `dxil.dll` mint DXIL; else the Windows SDK's `bin\<version>\x64`, never the first `dxc` on `PATH`).

| What | Version used | Kind | Notes | Install |
|---|---|---|---|---|
| Windows 10 or 11, x64 | Windows 11 build 26300 | Required | | |
| Visual Studio Build Tools 2026 (or any Visual Studio 2026 edition) | 18.10, MSVC 14.51.36231 | Required | The C++ build tools workload: MSVC, the C++ AddressSanitizer component (the `asan` flavor) and a Windows SDK. Full Visual Studio is not needed. | `winget install --id Microsoft.VisualStudio.BuildTools -e --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --add Microsoft.VisualStudio.Component.VC.ASAN --passive"` |
| Windows SDK | 10.0.26100.0 | Required | `windows.h`, D3D12, DXGI, DbgHelp, and `dxc.exe` with `dxcompiler.dll` and `dxil.dll`, which the DXIL minting uses. Comes with the Build Tools workload. | with the Build Tools |
| CMake | 3.21 or newer (4.4.3 used) | Required | | `winget install --id Kitware.CMake -e` |
| Ninja | 1.13 | Required | The generator the presets use. The one tool that must be on `PATH` (or named by `ENGINE_NINJA`). | `winget install --id Ninja-build.Ninja -e` |
| Git for Windows | 2.5x | Required | Its `bash` runs `scripts/test-phase.sh`. | `winget install --id Git.Git -e` |
| GPU driver with D3D12 (feature level 12_0) and Vulkan 1.3 | AMD 32.0.32015.7029 | Required to run | `vulkan-1.dll` ships with the driver. | vendor driver |
| LLVM (`clang-cl`, `lld-link`) | 23.1.5 | For checks | The second Windows configuration, which must keep building (Phase 13 rule). Found in `C:\Program Files\LLVM` without being on `PATH`. | `winget install --id LLVM.LLVM -e` |
| Graphics Tools (optional Windows feature) | | For checks | The D3D12 debug layer (`d3d12SDKLayers.dll`): `--validation` on DX12 and the debug-layer-silent checks. | Settings → System → Optional features → "Graphics Tools", or elevated: `Add-WindowsCapability -Online -Name Tools.Graphics.DirectX~~~~0.0.1.0` |
| Vulkan SDK | 1.4.363.0 | Required | Its headers (`vulkan/vulkan_core.h`) are needed to build; its validation layers serve `--validation`. The loader is already in the driver. | `winget install --id KhronosGroup.VulkanSDK -e` |
| `slangc` | **exactly v2026.18.2** | For checks | Same version as Linux, so minted SPIR-V and DXIL match byte for byte. winget's `shader-slang.slang` is older (2026.14.1); use the Windows x86-64 zip from https://github.com/shader-slang/slang/releases/tag/v2026.18.2 and put its `bin` on `PATH`, or set `ENGINE_SLANGC`. | GitHub release |
| Long paths (`HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled = 1`) | off on the developer PC (2026-11-07) | Optional | Lets the manifest's `longPathAware` take effect. The file layer works either way (it adds the `\\?\` prefix past 248 characters; `tests/io/io_windows_test.cpp` checks it with the setting off). The check with it on has not been done. An elevated system change: the developer makes it. | Group Policy "Enable Win32 long paths", or the registry value |
| Developer Mode | off on the developer PC | Optional | Lets tests create symbolic links without elevation; without it they skip with a reason (Phase 13 Days 151-152: the `Stat`/`Lstat` link checks). | Settings → System → For developers |
| Windows PowerShell | 5.1 (ships with Windows) | Required for checks | Runs `scripts/store-symbols.ps1`, `crash-report.ps1`, `find-dump.ps1` and `open-dump.ps1`, and the tests of them (Phase 13 Day 156). | part of Windows |
| WinDbg or the Debugging Tools for Windows (`cdb`) | not installed on the developer PC | Optional | `scripts/open-dump.ps1` opens a dump in one of them (it exits 66 and says what to install without); `crash-report.ps1` does not need them. | `winget install --id Microsoft.WinDbg -e`, or the Windows SDK feature "Debugging Tools for Windows" |
| PIX for Windows | current | Optional | GPU captures while debugging the DX12 backend. | `winget install --id Microsoft.PIX -e` |
| Python 3 | 3.x | Optional | `scripts/list-aborts.py` only. | `winget install --id Python.Python.3.14 -e` |

LuaJIT (Phase 27 Day 473) needs nothing beyond the table: CMake builds its host tools (`minilua`, `buildvm`) with the configured compiler and runs
them, as `msvcbuild.bat` would (DynASM with `-D WIN`, `buildvm -m peobj`), with MSVC and `clang-cl` alike. Do not run `msvcbuild.bat`.

There is no Windows ThreadSanitizer or UBSan: those flavors run on the Linux machine only.

## macOS (x86-64; arm64 compile-only): supported (Phase 21)

**Status (2026-11-13, Phase 21 Day 312): macOS builds and passes everything the Linux build has** (`cmake --preset macos-clang`), with the Cocoa
window and the Metal graphics backend (no Vulkan on macOS), in plain, asan and tsan; `cmake --preset macos-clang-arm64` builds every target for
Apple silicon without running anything (`ENGINE_COMPILE_ONLY`). `docs/guide/macos.md` is the guide. The versions below are a 2019
MacBook Pro (Intel Core i5-8257U, Intel Iris Plus Graphics 645, macOS 15.8.1), checked again on 2026-11-13; evidence in `docs/superpowers/specs/2026-11-11-macos-port-design.md`. Xcode is **not** needed: the Command Line Tools have the compiler, and
the Metal shaders are minted through the driver (below). Command-line tools come from [MacPorts](https://www.macports.org) on this machine;
Homebrew works the same way.

| What | Version used | Kind | Notes | Install |
|---|---|---|---|---|
| macOS on an x86-64 Mac with a Metal 3 GPU | 15.8.1 (24H32) | Required | The deployment target is macOS 13.3 (Day 284; floating-point `std::to_chars` needs 13.3, Metal 3 needs 13.0). Apple silicon is compiled, not run (no hardware): the `macos-clang-arm64` preset (Day 311). | |
| Command Line Tools for Xcode | 16.4 (Apple clang 17.0.0, `clang-1700.0.13.5`; macOS SDK 15.5) | Required | The only macOS compiler (Phase 21 rule). Also `lldb` (1700.0.9.502), `dsymutil`, `dwarfdump`, `atos`, `strip`, `nm`, `otool`, `lipo`, `leaks` and Apple's `git` (2.39.5); the crash and symbol scripts use them (Day 292), with `ditto` from macOS itself. Its `clang-format` (Apple 17.0.0) is **not** the formatter: see the next line. | `xcode-select --install` |
| `clang-format` | 23.1.5 (MacPorts `clang-23`; Linux uses 23.1.5) | Required to commit | `scripts/format.sh` and the pre-commit hook. MacPorts names it `clang-format-mp-23`, and `format.sh` and the hook call `clang-format`, so link it under that name (`ln -s /opt/local/bin/clang-format-mp-23 /usr/local/bin/clang-format`; `port select clang` would also put MacPorts' `clang++` first on `PATH`, which the cross-syntax tests call). 23.1.5 leaves the main line's tree unchanged (`format.sh --check`: 2,656 files, checked on 2026-11-12). The Command Line Tools' Apple `clang-format` 17.0.0 does not do: it formats three constructs unlike 23.1.x (`IsConstructible<D, E&&>` in a requires-clause, `ElfW(Phdr)& ph`, `char (*)[N]`) and does not read `.clang-format-ignore`. | `sudo port install clang-23` |
| `clang-tidy`, `run-clang-tidy` | 23.1.5 (MacPorts `clang-23`) | For checks | `scripts/tidy.sh`; linked as `clang-tidy` and `run-clang-tidy` the same way (`clang-tidy-mp-23`, `run-clang-tidy-mp-23`). Not yet run on the Mac. | `sudo port install clang-23` |
| CMake | 3.21 or newer (4.4.4 used) | Required | | `sudo port install cmake` (or `cmake-devel`) |
| Ninja | 1.13.2 | Required | The generator the `macos-clang` and `macos-clang-arm64` presets use. | `sudo port install ninja` |
| `bash` | 3.2.57 (`/bin/bash`) | Required | `scripts/test-phase.sh` and the other scripts run on 3.2: none uses a bash-4 feature. | part of macOS |
| `slangc` | **exactly v2026.18.2** | For checks | Re-mints shaders; same version as Linux and Windows, so minted SPIR-V matches byte for byte (checked on Day 283). Use the macOS x86-64 archive from https://github.com/shader-slang/slang/releases/tag/v2026.18.2 (`slang-2026.18.2-macos-x86_64.tar.gz`) unpacked to `~/tools/slang-2026.18.2`, or name it with `ENGINE_SLANGC`. **Watch out:** kitty (the terminal) ships an unrelated program called `slangc` in `kitty.app/Contents/MacOS`, which can come first on `PATH`: the configure looks in `~/tools/slang-2026.18.2` first and refuses a `slangc` that does not answer `-v` with a Slang version (then it uses the committed shader outputs and says so). | from the Slang GitHub release |
| The Metal shader minting | the driver's compiler (`metalfe-32023.622` on macOS 15.8.1 (24H32)) | For checks | **Interim, pending (the user's decision, 2026-11-10):** `.metallib` files are minted through the driver's compiler with the undocumented `-[MTLLibrary serializeToURL:error:]` by `tools/metal_mint` (built on macOS, Phase 21 Day 299), so no Xcode; `cmake --build build-macos --target shaders` mints them with `slangc`. The minted bytes depend on the macOS build (recorded in each `.metallib.inc`), and the route is decided again when the user has a new Mac (Phase 21 Day 282, `KNOWN-ISSUES.md`). Without it the committed `.metallib.inc` files are used, and a shader added elsewhere gets a placeholder until a Mac mints it. | part of macOS |
| Python 3 | 3.x (3.14.10 used) | Optional | `scripts/list-aborts.py` and `scripts/std-usage-report.py` only. | `sudo port install python314` |
| Xcode | not installed | Optional | Instruments and the Metal debugger for GPU captures. Not needed to build, test or mint. | App Store |

**LuaJIT on macOS (Phase 27 Day 474):** the vendored LuaJIT needs nothing installed beyond the Command Line Tools. `third_party/luajit/CMakeLists.txt`
builds its host tools for the host's processor (`OSX_ARCHITECTURES` = the host's, so the `macos-clang-arm64` tree on an Intel Mac runs x86-64 tools
that generate the arm64 interpreter), writes Mach-O assembly (`buildvm -m machasm`), and takes the deployment target that LuaJIT's Makefile asks for in
`MACOSX_DEPLOYMENT_TARGET` from `CMAKE_OSX_DEPLOYMENT_TARGET` (13.3). The file layer's directory watcher uses FSEvents from Core Services, a system
framework.

There is no LeakSanitizer on macOS (Apple clang): ASan aborts the process when `ASAN_OPTIONS` asks for `detect_leaks=1`, so leak checks stay on Linux;
`leaks --atExit -- <program>` is the macOS tool. ASan (with UBSan) and TSan both work, so macOS runs all three flavors.

## Android, iOS, consoles: not yet supported

Phases 29 (Android, iOS) and 30 (consoles, gated on dev-kit and NDA access). Each phase adds its section here when it is broken into days.

## The shared engine library (Phase 23)

`-DENGINE_BUILD_SHARED=ON` (default OFF) builds the whole engine as one loadable library behind the C API (`libengine.so.1`, `engine.dll`, `libengine.1.dylib`); Linux from Phase 23 Day 343 (Clang or GCC, CMake 3.24 or newer for `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`; every module and vendored library is then compiled position independent with hidden symbols), macOS from Day 355 (Apple clang and the same CMake; the system's `nm` for `abi_check`; the Cocoa test host is Objective-C, which the Command Line Tools compile), Windows on Day 354; static linking stays the default for games. It needs nothing beyond the toolchain each platform section lists. A sanitized shared engine loads only into a host built with the same sanitizer (`docs/superpowers/specs/2026-11-16-engine-library-design.md`).
