# Phase 13 / Day 150 — Test Tooling, Gates and Docs on Windows

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 149.

From tomorrow every day runs `scripts/test-phase.sh --affected` on Windows, so the test-scoping tooling, the source gates and the documentation tools have to
work there first, and the gates learn the Windows rules (where `windows.h`, D3D12 and the Win32 file and environment calls may appear).

## Tasks

- [x] **`scripts/test-phase.sh` on Windows** under Git for Windows' `bash` (a dev-time tool like CMake; the spec records the decision against a PowerShell
      port): build directory and preset per platform, `ctest` paths, `.exe` names, CRLF-free output; `--flavor all` means the flavors the platform has, and the
      summary line names the ones skipped (`flavors: plain,asan (tsan: not on Windows)`); Git Bash rewrites arguments that look like paths (seen 2026-11-07:
      `/link` and `/libpath:...` passed to `clang-cl` became file names), so the script and any test that passes `/`-style tool flags sets
      `MSYS2_ARG_CONV_EXCL` (or calls the tool through CMake), with a test that a `/flag` argument reaches a child program unchanged; `--affected` maps Windows-only directories to phase 13; the `display`
      group detects a desktop session through `os::QuerySessionState`'s Windows answer.
      *Done:* the platform comes from `uname -s`, the default build directory is `build-msvc`, `--flavor tsan` stops with a message and `--flavor all`
      prints `flavors: all (plain and asan, no tsan on Windows)` (no `;` in the note: the summary's fields are `;`-separated); `tests/module_map.txt` maps
      `engine/foundation/os/windows` (0, 13), `os/windows/debug` (7, 13) and `tests/windows` (13). No script passes a `/flag` today and the toolchain
      tests call the compilers through CMake, not bash, so the `/flag` test was not written (declined; the rule is in the guide). The `display` group needs
      nothing new: `SKIP_IF_NO_DISPLAY_SESSION()` already asks `os::QuerySessionState()` (Day 149); Windows has no window tests until Day 158.
- [x] **Platform-specific tests in the phase map:** file patterns need no qualifier (every source file exists in both checkouts; only the built tests
      differ). What differs per platform is which phases have tests in which flavors: every phase gets a `windows:` `flavors` line (a new `none` for a phase
      with no Windows tests yet), which each later day updates as its layer joins the Windows build, so a flavor cannot vanish unnoticed there either. The
      unused-line check (`CHECK_UNUSED`) runs on Linux only: a `test:` line for a test Windows does not build yet is not unused. `test_phase_labels` checks
      every built test has one label on the platform it runs on. Self-tests for both platforms run on either machine.
      *Done:* `windows:` lines for phases 0-12 (`none` for 2, 3 and 8), `none` accepted by `tests/phase_labels/check.cmake`; `CHECK_UNUSED` only when
      `ENGINE_PLATFORM` is `linux`. Found in the review: the check also takes `BUILT_FLAVORS` (from `ENGINE_HAVE_ASAN`/`ENGINE_HAVE_TSAN`), so `clang-cl`,
      which has no ASan flavor, is not asked for ASan tests; self-test `test_phase_labels_built_flavors`.
- [x] **`tests/test_phase_script`** passes on Windows (path separators, `bash` from Git, temporary directories through the file layer's rules).
      *Done:* `bash` is found beside `git.exe` in any of Git for Windows' layouts (`cmd/`, `bin/`, `mingw64/bin/`) and passed to the cases as `-DBASH`; a missing
      `bash` stops the configure. A case whose phase has no tests on this platform passes when ctest agrees the selection is empty and the script chose the
      expected phases. On Windows the sanitizer cases use phase 13, `--flavor tsan` is a refusal case, and the flavor partition runs over phases 0, 10 and 13 in
      plain and asan. The cases use fixture files, no temporary directories.
- [x] **Gates on Windows and for Windows code** (each gate's self-tests gain planted Windows violations):
      - `platform_neutral_check`: `windows.h`, `winternl.h`, `dxgi*.h`, `d3d12*.h`, `shlobj.h`, `dbghelp.h` only under the Windows directories of the phase
        file's rule, and only through `os/windows/win32.h` (or `rhi/dx12/d3d12_api.h`, added Day 164).
      - `module_deps_check` (found on Day 147, spec section 3 item 3): a neutral file may not include a platform system header (`xkbcommon/`, `wayland-`,
        `X11/`, `windows.h` and the Win32 headers, `d3d12*.h`, `dxgi*.h`, `vulkan_win32.h`, `vulkan_wayland.h`, `vulkan_xlib.h`); `platform/key_translate.cpp` is
        allow-listed until Day 159 moves its XKB part.
      - `rhi_boundary_check`: D3D12/DXGI names and headers only in `engine/rhi/dx12/` (as Vulkan's are only in `engine/rhi/vulkan/`).
      - `file_access_check`: the Win32 and CRT file calls (`CreateFileW`, `CreateFileA`, `FindFirstFile*`, `MoveFileEx*`, `ReplaceFile*`, `DeleteFile*`,
        `CreateDirectory*`, `GetFileAttributes*`, `GetFullPathName*`, `_wfopen`, `_open`, `_wopen`, `fopen_s`) are violations outside the allow list; the Windows
        file backend and the `os` descriptor primitives are allow-listed with reasons (from Day 149: `engine/foundation/os/windows/os.cpp`, which
        implements them with `CreateFileW`, and `tests/windows/os_windows_test.cpp`, which tests them on ordinary files and cleans up with `DeleteFileA`).
      - `config_access_check`: `GetEnvironmentVariable*`, `_wgetenv`, `_dupenv_s`, `GetCommandLine*`, `CommandLineToArgvW` outside the settings system and `os/`.
      - `signal_safety_check`: the Windows crash hook's code is marked `SIGNAL-SAFE` and checked by the same rules (no allocation, no locks, no CRT formatting);
        the rule text says what "signal-safe" means on Windows (code that runs inside a crashed process's exception handler).
      - `std_container_check`, `raw_new_check`, `settings_fixtures_check`: run unchanged on Windows; fix path handling only.
      *Done:* all nine gates are in the Windows build. `module_deps_check` has a `platform-header <regex>` directive (neutral files) with a `header` allow-list
      kind and a fixture tree; `rhi_boundary_check` knows the D3D12 and DXGI names and headers (planted in `game/` and in `rhi/vulkan/`) and skips every gate's
      `fixtures/`; `file_access_check` and `config_access_check` know the Win32 and C runtime names (planted `win_leak.cpp`), with the two Day 149 allow-list
      entries; `signal_safety_check` forbids the Win32 heap, lock, formatting, `OutputDebugString` and `LoadLibrary` calls in signal-safe code (planted
      `win_handler.cpp`); `platform_neutral_check` has rule 4 (planted `os/linux/win_in_linux.cpp`, `os/windows/direct.cpp`). No Windows code is signal-safe
      yet: the crash hook is Day 155's, which marks it. `config_access_check` exempts no directory (not `os/` as written above): the Windows entry point gets
      an allow-list entry on Day 158 (added to that day file). The gates print each problem on one line through `gate_problem` (`message(NOTICE)`):
      `message(SEND_ERROR)` wraps at the console width, so the self-tests depended on the checkout path's length on Windows.
- [x] **Moved on Day 150 (dependency order):** `tools/docgen` and `ctest -L docs` on Windows go to Day 157, `tools/crash_extract` stays on Day 156 and
      `tools/shader_mint` on Day 163. `docgen` and `crash_extract` link the whole foundation (and `docgen` installs the debugging hooks), whose file,
      debugging and settings layers arrive on Days 151-157; `shader_mint` links the renderer.
- [x] **`tests/phase_labels` in the Windows build** (Day 148 left it out): with the platform rule above, `test_phase_labels` runs on Windows with
      `-DPLATFORM=windows` and every phase has the per-platform `flavors` lines it needs there.
- [ ] (Moved to Day 163 with `tools/shader_mint`, the only tool that throws; added to that day file.) **Exceptions for tools that throw** (Day 148 review): `tools/shader_mint` (nlohmann/json) gets `ENGINE_EXCEPTIONS_COMPILE_OPTIONS`; any other tool or
      vendored library that throws gets it too.
- [x] (Moved: `tools/shader_mint` builds on Windows on Day 163, `tools/crash_extract` on Day 156; see above.)
- [x] **Documentation:** `docs/guide/building-and-testing.md` gets "Building and testing on Windows" (presets, Git Bash, flavors, display group, the two-machine
      rule); `ctest -L docs` on both machines. *Done:* the section is written; `ctest -L docs` moves with `docgen` to Day 157 on Windows and to the
      phase close (Day 174) on Linux.
- [x] **Runs:** Windows `scripts/test-phase.sh --affected` and `--flavor asan`; Linux `scripts/test-phase.sh --affected` (the phase map changed, so everything)
      and asan/tsan; quote all lines. *Windows, after the review fixes:* `scripts/test-phase.sh --affected` printed
      `ran 247 of 287 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 12 s`, `--affected --flavor asan` printed
      `ran 40 of 287 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 2 s`, and `--all --flavor all` printed
      `ran 287 of 287 tests (phases: all; flavors: all (plain and asan, no tsan on Windows); display: included; crash tests: included) in 13 s`. `ctest --preset windows-msvc` 287 of 287,
      `ctest --preset windows-clang-cl` 278 of 278. Linux: at the phase close (Day 174), listed below.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: a gate that passes because it scanned nothing (each gate's self-test proves it found its
      planted Windows violation on both platforms), and phase labels that disappear on one platform unnoticed.
- [x] Update `STATUS.md` (test commands on Windows), `KNOWN-ISSUES.md`; commit and push.

**Done:** `scripts/test-phase.sh` runs on Windows and reports skipped flavors, the phase map knows platform-only tests, every gate runs on Windows and catches
planted Windows violations. (The identical generated reference moved with `docgen` to Day 157.) Guide touched: `docs/guide/building-and-testing.md`
("Building and testing on Windows", the phase table's row 13).

- **For the Linux run at the phase close:** shared files changed today: `scripts/test-phase.sh`, `tests/phases.txt`, `tests/module_map.txt`,
  `tests/phase_labels/` (`check.cmake`, `CMakeLists.txt`), `tests/test_phase_script/` (`CMakeLists.txt`, `check.cmake`, `partition.cmake`), every gate under
  `tools/*_check/` (scripts, allow lists, fixtures, `CMakeLists.txt`), the root `CMakeLists.txt` (Windows block only) and the guide. On Linux run
  `scripts/test-phase.sh --affected` (everything: the phase map changed), its asan and tsan flavors, and `ctest -L docs`.

## Post-implementation code review

`/code-review high` over the day's diff: **10 findings, 7 fixed, 3 declined.**

Fixed: (1) `bash` was looked up only beside `cmd/git.exe`, and a missing `bash` returned quietly, so all 30 `test_phase_script` tests were missing from the
Windows build while ctest reported everything passing: every Git layout is searched now and a missing `bash` stops the configure. (2) The Windows flavor
note contained a `;`, which split the summary's fields (`test_phase_script_everything` failed once the tests ran). (3) The tsan case and the flavor
partition assumed three flavors and phases with no Windows sanitizer tests. (4) `test_phase_labels` demanded ASan tests from `clang-cl`, which builds none
(`BUILT_FLAVORS`, with a self-test). (5) `rhi_boundary_check` scanned the other gates' planted fixtures (the new D3D12 includes). (6) `gate_problem` and
`module_deps_check`'s `problem` were macros, so a reported source line was evaluated again (`\w`, `${x}`): now functions; the planted line
`fopen(R"(C:\w\${x})", "r")` must print verbatim (it did not print at all with the macro, checked). (7) Rule 4 missed a header named with a folder
(`<um/Windows.h>`, planted).

Declined: (8) the plan said `config_access_check` allows `os/`; the gate exempts no directory, and the one file that will need it (the Windows entry point,
Day 158) gets its own allow-list entry with a reason, as every other exception does. (9) No Windows code is between `SIGNAL-SAFE` markers yet: the crash hook
is written on Day 155, whose file already requires the markers. (10) The flavor check trusts `ENGINE_HAVE_ASAN`; for MSVC it is a constant, and a lost ASan
flavor would still show in `scripts/test-phase.sh --flavor asan` refusing an empty selection.
