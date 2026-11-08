# Phase 13 / Day 174 — Guides, Closing Full Runs on Both Machines & Wrap-up

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Days 147-173.

Finish the documentation, run everything on both machines, review the whole phase, close it.

## Tasks

- [x] **New guide `docs/guide/windows.md`:** prerequisites (with the versions from Day 147), how the build finds the compilers and SDK (and the override
      variables), presets, building, running tests and demos, the debug layer and validation layers, crash dumps and symbols, where settings live, known gaps;
      listed in `docs/guide/README.md`.
- [x] **Guide pass** (Windows; the Linux sample run and reference comparison are in the Linux checklist below) (read each as a new developer, fix what is stale): `getting-started.md`, `building-and-testing.md`, `platform-os.md`,
      `platform-windowing-and-input.md`, `io.md`, `settings.md`, `jobs.md`, `foundation-debugging.md`, `porting-debugging.md`, `rhi.md`, `rhi-shaders.md`,
      `renderer.md`, `render-settings.md`; every tagged sample compiles and runs on both machines; `docs/reference/modules.txt` covers every new public header;
      `docgen lint` zero; reference regenerated (identical on both machines) and committed.
- [x] **Project documents** (the Linux table of `BUILD.md` is checked on the Linux machine): `BUILD.md` (Windows section marked supported, every version checked on the PC, the Linux table checked on the Linux machine); `CLAUDE.md` (Windows build and test commands; the Windows directories rule; flavors per platform; the two-machine rule if it stays
      for later phases; the DbgHelp exception to the debugging-library rule; `windows.h` only through `win32.h`); `PLAN.md` (assumption #7 gains Windows x64; #2
      states the UCRT; #18 states DXIL minting; #20 the DbgHelp resolver; roadmap row; progress-log entry); `shaders/README.md` current.
- [x] **Known-issues sweep:** every Phase 13 deferral filed (enhanced barriers, placed resources, HDR10, exclusive fullscreen, IME composition, ARM64, fast-fail
      crashes, anything left by a day), every entry this phase fixed removed (the backend registry and switch, the HLSL object index, the D3D12 Y flip, `--permute`
      on Windows, the portable `u128` under MSVC, the Breakpad Windows headers, `env_source.cpp`'s `environ`, share modes).
- [x] **The deferred Linux checks** (the two-machine rule as amended on 2026-11-07): everything each day filed for the Linux machine in `KNOWN-ISSUES.md`
      (Days 147 onward): both unity builds, the CMake version, the reference regeneration with `docgen lint`, the phase-label self-tests, the shared-code
      changes of every day; fix what fails, with the day it came from named in the fix.
- [x] **Closing full runs (CLAUDE.md, required for a phase's last day):** Windows `scripts/test-phase.sh --all --flavor all` (plain and asan; the summary names tsan
      as not on Windows), `ctest -L docs`; Linux `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`; both demos 1000 frames with validation on DX12 and
      Vulkan (Windows) and on Wayland and X11 (Linux); record both final `ran X of Y tests (phases ...)` lines in this file; nothing left out on either machine.
- [x] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (from the commit before Day 147 to the
      working tree); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review"; fixes re-run on both
      machines.
- [x] Update `plans/phase-13-windows-dx12.md` (status complete, days done), `PLAN.md`, `STATUS.md`; `CURRENT_PHASE` stays 13 until Phase 14 has its day files and
      starts (policy); commit and push.

**Done:** `docs/guide/windows.md` and every touched guide are current and identical in output on both machines, the closing lines of the full runs on Windows
(plain, asan) and Linux (all flavors) are recorded with every test passing or a filed known issue, the demos run on all four backend and display combinations, the
uncapped review is recorded, and Phase 13 is marked complete.

## Notes

- **Guides:** new `docs/guide/windows.md` (prerequisites, how the build finds the compilers and the override variables, presets and trees, tests, the
  demos and `--rhi`, validation and the debug layer, crash dumps and the symbol store, where things live, known gaps), listed in `docs/guide/README.md`.
  Stale text fixed in `building-and-testing.md` (the Windows section: everything is ported; clang-cl's full run at a phase close; the two-machine
  paragraph), `getting-started.md` (the demos draw with Direct3D 12 by default), `foundation-debugging.md` (status: Windows implemented). The other
  guides of the list were read for Windows and DX12 statements and are current (`rhi.md`, `renderer.md`, `render-settings.md` and `rhi-shaders.md` were
  updated on their days; `platform-os.md`, `platform-windowing-and-input.md` and `jobs.md` are still skeletons with nothing Windows-specific to
  correct). Reference regenerated on Windows: 4492 of 4492 documented, `docgen lint` zero, `ctest -L docs` 6 of 6.
- **Project documents:** `BUILD.md` (Windows supported, the status line, a duplicated WinDbg row removed); `CLAUDE.md` (a "Windows is a supported
  platform" section: the build and test commands, flavors per platform, Windows code in Windows directories and `windows.h` through `win32.h`, the
  two-machine rule kept for later phases, the backend chosen at start; the RHI bullet names DX12; DbgHelp on Windows in the debugging bullet); `PLAN.md`
  (assumption #2 the UCRT, #7 Windows x64, #18 DXIL minting, the blob sets, `ENGINE_CLIP` and the object index stream, #20 the DbgHelp resolver; the
  roadmap row); `shaders/README.md` was already current.
- **Known issues:** one entry lists the Phase 13 deferrals (legacy barriers only, committed resources only, borderless fullscreen only, no IME
  composition, x64 only, HDR10, fast-fail crashes). The file-layer entries no longer list Windows as missing; share modes are settled (all three share
  flags), locks stay open. The entries this phase fixed (the HLSL object index, the D3D12 Y flip, `--permute` on Windows, `env_source.cpp`'s `environ`)
  had already gone on their days; the backend switch entry went on Day 172. The day entries "open until the Linux run" stay until that run.
- **Closing runs on Windows:** `scripts/test-phase.sh --all --flavor all`: `ran 2492 of 2492 tests (phases: all; flavors: all (plain and asan, no tsan on Windows); display: included; crash tests: included) in 334 s`, one failure: the asan copy of `Windows: while the crash
  handler is installed, abort() shows no dialog ...`, whose child did not exit within its 60 s deadline (passes three times alone; filed). After the
  review's fixes (which changed the phase map) the full plain suite again: `ran 1416 of 2492 tests (phases: all; flavors: plain; display: included; crash tests: included) in 312 s`, one failure, the filed throughput flake. `ctest -L
  docs` 6 of 6. Both demos 1000 frames with `--validation` on DX12 and on Vulkan: exit 0, nothing from either validation layer. Earlier the same day
  (Day 173) clang-cl's full plain suite passed and MSVC with unity off had only the filed device-removal failures.

## Post-implementation code review

`/code-review high --max-findings all` over the whole phase's diff (`339c375~1` to the working tree; 631 files, about 18500 hand-written lines under
`engine/`, `game/`, `tools/` and `scripts/`; every day had its own review). The review reports at most 32 findings; it found 11: 5 fixed, 6 declined.

- **Fixed:** the Day 173 `test:` lines for the Windows-only DX12 demo tests would fail the phase-label check's unused-line rule on Linux (now
  `testopt:`, like `windows_*`); the adapter-switch test's helpers were unused on Linux, an error under `-Werror` (now inside the Windows condition);
  a recovery test kept the old device in `before` and discarded it with `(void)before`, so it never checked that a new device was made (now compares
  the epochs); the first draft of `windows.md` put the user's settings in the registry (they are `settings.cfg`, as on Linux); `BUILD.md` still said
  "being ported" and listed WinDbg twice.
- **Declined (filed):** the asan crash-handler test's timeout under load; the intermittent `DXGI_ERROR_DEVICE_REMOVED` at device creation and the DX12
  reset loop's 64 KiB device-memory drop (Day 173 entries).
- **Declined:** `Dx12Device::Submit` returns 0 when the prologue of a later list in a batch cannot be built although earlier lists already ran. That
  happens only when an internal command list cannot be made (the device is lost or out of memory), and the frame is lost then anyway. The graphics and
  compute pipeline-cache lookups are near copies; they call different D3D12 functions with different description types, and sharing them would need a
  template for two short functions. `Submit` allocates a small barrier vector per call; one allocation is negligible next to the D3D12 calls in it.

## The Linux checklist (done 2026-11-08, Linux machine)

Everything Phase 13 filed for Linux, in one list. Run it from a checkout at this commit, fix what fails (naming the day it came from), and record the
lines here; then the phase can be marked complete.

1. Configure and build both unity modes with Clang **and** GCC (`-DENGINE_UNITY_BUILD=OFF` too): every target, warnings as errors.
2. Check the CMake version `BUILD.md` names, and the Linux table of `BUILD.md`.
3. `scripts/test-phase.sh --all --flavor all` (plain, asan, tsan): record the `ran X of Y tests (phases ...)` line.
4. `ctest -L docs`, and `cmake --build build --target docs`: the reference must come out byte-identical to the committed `docs/reference/`
   (`git status docs/reference` clean); `docgen lint` zero; every guide sample compiles and runs.
5. The phase-label self-tests and `test_phase_labels` with the unused-line check (the `testopt:` lines of Phase 13).
6. Both demos 1000 frames with `--validation` on Wayland and on X11 (`--backend wayland`, `--backend x11`); `--rhi vulkan` and `auto` (Vulkan on
   Linux); `--rhi dx12` must be refused with the reason.
7. The shared code of every day, as each day file's "For the Linux run" section lists it (Days 148-173), in particular: the per-backend GPU tests
   (`backend_fixture.h`, `*_gpu_test.cpp`, `reset_rig.h`, the renderer tests), the Vulkan swapchain's `ReleaseImages` and in-frame rebuild (Day 169),
   the neutral ring allocator (Day 165), `shaders/mipgen.comp` and the shaders minted on Windows (Days 163, 168: `ctest -R shaders_check` with `slangc`
   v2026.18.2 must reproduce them byte for byte), the backend registry (Day 162), the restart-required `backend` setting and the demos' `--rhi`
   (Day 173), and the settings, file-layer and debugging changes of Days 149-161.
8. Then remove the "open until the Linux run" entries of `KNOWN-ISSUES.md` that passed, record the lines in this file, and mark Phase 13 complete in
   `plans/phase-13-windows-dx12.md`, `PLAN.md` (the roadmap row and a progress-log entry) and `STATUS.md`. `CURRENT_PHASE` stays 13 until Phase 14
   has its day files and starts.

### Linux results (2026-11-08)

1. All four configurations build with no error and no warning of ours: Clang unity (`build/`), Clang `-DENGINE_UNITY_BUILD=OFF`, GCC unity, GCC non-unity (Clang 23.1.4, GCC 16.2.1).
2. `BUILD.md`'s Linux table checked: CMake 4.4.5 (3.21 minimum holds), Clang 23.1.4, wayland-scanner 1.26.0, wayland-protocols 1.50 (the table said 1.49: corrected), `slangc` v2026.18.2, Ninja 1.13.2.
   `dxc` is not installed on this machine; `shaders_portability` passes without it.
3. `scripts/test-phase.sh --all --flavor all`: `ran 3606 of 3606 tests (phases: all; flavors: all; display: included; crash tests: included) in 105 s`, no failure.
4. `cmake --build build --target docs`: 4492 of 4492 documented, `git status docs/reference` clean (identical to the Windows output), `docgen lint` zero; `ctest -L docs` 6 of 6.
5. `test_phase_labels` and its self-tests pass (the `testopt:` lines hold).
6. `renderer_demo` and `rhi_demo`, `--frames 1000 --validation`, on `--backend wayland` and `--backend x11`: exit 0, no validation message. `--rhi vulkan` and `--rhi auto` run; `--rhi dx12` is refused
   (exit 1) with `the dx12 backend is not registered in this program`. That message suggests a missing link step, though on Linux the backend is simply not built; filed as a wording issue.
7. Shared code of Days 148-173: covered by the full run above; `ctest -R shaders_` (`shaders_check`, with `slangc` v2026.18.2 re-minting every shader byte for byte, and `shaders_portability`) passes.
8. The "open until the Linux run" entries of `KNOWN-ISSUES.md` (28) are removed.
