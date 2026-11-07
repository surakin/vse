# Phase 12 / Day 143 — Test Tooling, Gates and Docs on Windows

**Phase:** [Phase 12 — Windows Port + DX12 Backend](../phase-12-windows-dx12.md)
**Depends on:** Day 142.

From tomorrow every day runs `scripts/test-phase.sh --affected` on Windows, so the test-scoping tooling, the source gates and the documentation tools have to
work there first, and the gates learn the Windows rules (where `windows.h`, D3D12 and the Win32 file and environment calls may appear).

## Tasks

- [ ] **`scripts/test-phase.sh` on Windows** under Git for Windows' `bash` (a dev-time tool like CMake; the spec records the decision against a PowerShell
      port): build directory and preset per platform, `ctest` paths, `.exe` names, CRLF-free output; `--flavor all` means the flavors the platform has, and the
      summary line names the ones skipped (`flavors: plain,asan (tsan: not on Windows)`); Git Bash rewrites arguments that look like paths (seen 2026-11-07:
      `/link` and `/libpath:...` passed to `clang-cl` became file names), so the script and any test that passes `/`-style tool flags sets
      `MSYS2_ARG_CONV_EXCL` (or calls the tool through CMake), with a test that a `/flag` argument reaches a child program unchanged; `--affected` maps Windows-only directories to phase 12; the `display`
      group detects a desktop session through `os::QuerySessionState`'s Windows answer.
- [ ] **Platform-specific test files:** `tests/phases.txt` gains a platform qualifier for files that exist on one platform only (`*_linux_test.cpp`,
      `*_windows_test.cpp`, the Wayland/X11 tests, later `tests/rhi/dx12_*`); `test_phase_labels` checks every built test has one label on the platform it runs on,
      and that the `flavors` lines hold per platform (a `flavors 12 plain,asan` line is Windows; a phase may list `linux:` and `windows:` flavor sets). Self-tests
      for both platforms run on either machine (the checks read the map, not the build).
- [ ] **`tests/test_phase_script`** passes on Windows (path separators, `bash` from Git, temporary directories through the file layer's rules).
- [ ] **Gates on Windows and for Windows code** (each gate's self-tests gain planted Windows violations):
      - `platform_neutral_check`: `windows.h`, `winternl.h`, `dxgi*.h`, `d3d12*.h`, `shlobj.h`, `dbghelp.h` only under the Windows directories of the phase
        file's rule, and only through `os/windows/win32.h` (or `rhi/dx12/d3d12_api.h`, added Day 157).
      - `rhi_boundary_check`: D3D12/DXGI names and headers only in `engine/rhi/dx12/` (as Vulkan's are only in `engine/rhi/vulkan/`).
      - `file_access_check`: the Win32 and CRT file calls (`CreateFileW`, `CreateFileA`, `FindFirstFile*`, `MoveFileEx*`, `ReplaceFile*`, `DeleteFile*`,
        `CreateDirectory*`, `GetFileAttributes*`, `GetFullPathName*`, `_wfopen`, `_open`, `_wopen`, `fopen_s`) are violations outside the allow list; the Windows
        file backend and the `os` descriptor primitives are allow-listed with reasons.
      - `config_access_check`: `GetEnvironmentVariable*`, `_wgetenv`, `_dupenv_s`, `GetCommandLine*`, `CommandLineToArgvW` outside the settings system and `os/`.
      - `signal_safety_check`: the Windows crash hook's code is marked `SIGNAL-SAFE` and checked by the same rules (no allocation, no locks, no CRT formatting);
        the rule text says what "signal-safe" means on Windows (code that runs inside a crashed process's exception handler).
      - `std_container_check`, `raw_new_check`, `settings_fixtures_check`: run unchanged on Windows; fix path handling only.
- [ ] **`tools/docgen` on Windows:** its POSIX includes go (it uses the file layer or a documented tool boundary); `cmake --build --target docs` produces a
      `docs/reference/` byte-identical to the Linux output (diff in a test: generate on Windows, compare with the committed tree); `ctest -L docs` passes on
      Windows, including the compiled guide samples.
- [ ] **`tools/shader_mint`, `tools/crash_extract`** build on Windows (the `--permute` process start is Day 156's; until then the permute path reports "not on
      Windows yet" and its tests skip with that reason).
- [ ] **Documentation:** `docs/guide/building-and-testing.md` gets "Building and testing on Windows" (presets, Git Bash, flavors, display group, the two-machine
      rule); `ctest -L docs` on both machines.
- [ ] **Runs:** Windows `scripts/test-phase.sh --affected` and `--flavor asan`; Linux `scripts/test-phase.sh --affected` (the phase map changed, so everything)
      and asan/tsan; quote all lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: a gate that passes because it scanned nothing (each gate's self-test proves it found its
      planted Windows violation on both platforms), and phase labels that disappear on one platform unnoticed.
- [ ] Update `STATUS.md` (test commands on Windows), `KNOWN-ISSUES.md`; commit and push.

**Done:** `scripts/test-phase.sh` runs on Windows and reports skipped flavors, the phase map knows platform-only tests, every gate runs on Windows and catches
planted Windows violations, and the generated reference is identical on both machines.
