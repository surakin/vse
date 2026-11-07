# Phase 12 / Day 141 — CMake and the Compiler on Windows

**Phase:** [Phase 12 — Windows Port + DX12 Backend](../phase-12-windows-dx12.md)
**Depends on:** Day 140 (Spike C's failure list, the toolchain decision).

Make the tree configure and compile with MSVC (and `clang-cl`) up to the platform code, which arrives tomorrow. Shared code changes today, so the Linux
machine must stay green.

## Tasks

- [ ] **Root `CMakeLists.txt`:** replace the "Only x86-64 Linux" stop with a supported-platform check (Linux x86-64, Windows x86-64; anything else stops
      with a message naming the phase that ports it); `-Wl,--build-id` stays Linux; Windows links with `/DEBUG:FULL` and compiles with `/Zi` (or `/Z7`
      under sccache, recorded) so every program has a PDB and a CodeView record; `ENGINE_SPLIT_DEBUG_INFO` documents that Windows always has separate PDBs.
- [ ] **Per-compiler options:** `cmake/EngineCompilerOptions.cmake` defines `ENGINE_PRODUCTION_COMPILE_OPTIONS` and the warning set per compiler (GCC/Clang:
      today's flags; MSVC and clang-cl: `/permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHs-c- /GR- /W4` plus `_HAS_EXCEPTIONS=0`, and the
      stack-protector equivalent `/GS-` only where Linux uses `-fno-stack-protector`), the sanitizer options (`foundation_asan`: `/fsanitize=address` on
      MSVC; `foundation_tsan` and the tsan canary are not defined on Windows, and the targets that link them are skipped with a configure message), and
      `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `UNICODE`, `_UNICODE` for Windows targets. Catch2's targets keep exceptions on.
- [ ] **The build finds its compilers by itself** (developer requirement, 2026-11-07): configuring from a plain PowerShell or Git Bash, with no Developer
      Command Prompt, no `vcvars*.bat` and no `INCLUDE`/`LIB`/`PATH` set by hand, must work. Toolchain files `cmake/toolchains/windows-msvc.cmake` and
      `windows-clang-cl.cmake` locate everything themselves:
      - Visual Studio with `vswhere.exe` (at its fixed path `%ProgramFiles(x86)%/Microsoft Visual Studio/Installer/`) and
        `-latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`, so Build Tools, Community, Professional
        and Enterprise all qualify; the MSVC version from `VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt`, `cl.exe` and `link.exe` from
        `VC/Tools/MSVC/<version>/bin/Hostx64/x64/`.
      - The Windows SDK from the registry (`HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots`, `KitsRoot10`) with the newest version directory under
        `Include/` that has `um/windows.h`; `rc.exe` and `mt.exe` from its `bin/<version>/x64/`.
      - `clang-cl.exe` and `lld-link.exe` from Visual Studio's `VC/Tools/Llvm/x64/bin` first, then an LLVM install (`HKLM\SOFTWARE\LLVM\LLVM`, then
        `%ProgramFiles%/LLVM/bin`).
      - The include and library directories (MSVC `include`/`lib/x64`, SDK `ucrt`, `um`, `shared`, `winrt` and their `Lib/<version>/{ucrt,um}/x64`) set through
        `CMAKE_<LANG>_STANDARD_INCLUDE_DIRECTORIES` and the link directories, and the tool directories added to the build's environment where the tools need
        them (ASan's runtime DLL beside the test executables, so tests run without a developer `PATH`).
      - Overrides for unusual machines: `ENGINE_VS_INSTALL_DIR`, `ENGINE_WINDOWS_SDK_DIR`/`ENGINE_WINDOWS_SDK_VERSION`, `ENGINE_LLVM_DIR` cache variables;
        a clear configure error that names what was searched when something is missing.
      - If the environment already comes from a Developer Command Prompt, the found compiler must be the same one (or the override wins, with a message), never a
        silent mix of two toolsets.
      - A ctest (`windows_toolchain_discovery`, phase 12) configures a scratch project with the environment stripped of `INCLUDE`, `LIB`, `LIBPATH`,
        `VCINSTALLDIR`, `WindowsSdkDir` and the VS directories on `PATH`, for both toolchain files, and builds and runs a hello-world that includes `windows.h`.
- [ ] **CMake presets** (`CMakePresets.json`): `linux` (today's defaults), `windows-msvc` (Ninja, the MSVC toolchain file), `windows-clang-cl` (Ninja, the
      clang-cl toolchain file), each with a build directory of its own; Ninja itself is found on `PATH` or through `ENGINE_NINJA` (the one tool the developer
      installs on `PATH`); `STATUS.md` build commands updated for both platforms (no developer prompt in them).
- [ ] **The application manifest:** `cmake/engine.manifest` (UTF-8 active code page, `longPathAware`, per-monitor-v2 DPI, Windows 10/11 `supportedOS`) and a
      function `engine_add_manifest(<target>)` called from every `add_executable` helper, so tests, tools and demos all carry it.
- [ ] **`windows.h` wrapper:** `engine/foundation/unity/windows_enable.h` / `windows_disable.h` (push and pop the macros `windows.h` defines that collide with
      engine names: at least `LoadLibrary`, `CreateWindow`, `GetObject`, `CreateFile`, `CopyFile`, `MoveFile`, `DeleteFile`, `CreateDirectory`, `RemoveDirectory`,
      `GetCurrentDirectory`, `SetCurrentDirectory`, `GetEnvironmentVariable`, `near`, `far`, `min`, `max`, `ERROR`, `DELETE`, `IN`, `OUT`, `OPTIONAL`, `interface`;
      the exact list is Spike C's) and `engine/foundation/os/windows/win32.h`, the one header Windows code includes. A test (`windows_header_test.cpp`,
      phase 12) includes `win32.h` and then every foundation header in one unity batch and checks it compiles; a fixture with the wrapper removed fails to
      compile (a `WILL_FAIL` build test, as Phase 10 does for the type checks).
- [ ] **Compiler portability in shared headers:** `debug/compiler.h` gains MSVC spellings (`__forceinline`, `__declspec(noinline)`, `__assume`,
      `_ReturnAddress`, `__debugbreak`, `_BitScanForward64`/`__lzcnt64`/`__popcnt64` behind the existing macro names, `__restrict`), the printf-format
      attribute becomes a macro that is empty on MSVC (with `_Printf_format_string_` where it helps), `int128.h` takes the portable path on MSVC
      (`_umul128`/`_addcarry_u64` intrinsics where the portable code allows, measured against the plain portable code), `types.h` checks its static asserts
      under MSVC (`long` is 32 bits on Windows: audit every `long` in shared headers; `os::Write`/`Read` return `long` today — keep or change to `isize`,
      decided here and recorded).
- [ ] **Every other failure class from Spike C** fixed at its cause in shared code (no `#ifdef _MSC_VER` scattered through engine code: a portability
      helper in `compiler.h` or a per-platform source file instead); POSIX-only sources are excluded from the Windows source lists, not stubbed.
- [ ] **Build check:** `cmake --preset windows-msvc && cmake --build --preset windows-msvc --target foundation_core` (or whichever library holds the
      platform-free foundation; name it) compiles every foundation source except `os/`, `io/` backends and `debug/` backends, unity on and off.
- [ ] The `clang-cl` preset compiles the same set; record the warnings it adds that MSVC did not.
- [ ] **Linux:** both unity modes build; `scripts/test-phase.sh --affected` (a `CMakeLists.txt` and foundation headers changed, so everything runs) and the
      asan and tsan flavors; quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: macros that silently change meaning between compilers (`ENGINE_ASSUME`, the bit-scan
      helpers' behavior at zero, `long` widths), and the manifest being on every executable.
- [ ] Update `STATUS.md` (build commands for both platforms) and `KNOWN-ISSUES.md` (the remaining Windows link failures until Day 142); commit and push.

**Done:** the tree configures with MSVC and `clang-cl` from a plain shell (the toolchain files find Visual Studio, the Windows SDK and clang-cl themselves,
proven by the stripped-environment test), the platform-free foundation compiles on Windows in both unity modes, the `windows.h` wrapper and its
test exist, the portable `u128` path compiles under MSVC, and the Linux suite still passes in every flavor.
