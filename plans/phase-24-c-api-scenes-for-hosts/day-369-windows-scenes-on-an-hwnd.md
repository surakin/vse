# Phase 24 / Day 369 — Windows: Scenes on an `HWND` (Vulkan & Direct3D 12)

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 358-368 built on Linux; Phases 13, 23 Day 354. **Done on the Windows PC.**

The scene features on Windows, on both backends.

## Tasks

- [x] Build and run every test of Days 359-368 on `windows-msvc` (plain and asan) and `windows-clang-cl` (plain), shared and static; fix what differs.
- [x] Golden images on Vulkan and on Direct3D 12 under the tolerance of Day 358; the differences between the two backends are measured and recorded (not hidden by a larger tolerance).
- [x] Host textures on Direct3D 12 (upload heaps, resource states), labels, skinned meshes and the reset test with the D3D12 device-removed path.
- [x] The Win32 C host: a message loop that forwards input and implements the callbacks, driving the scene (the walk-around sample of Day 371 compiles here).
- [x] Record in `KNOWN-ISSUES.md` what could not be done and anything that moved from Linux.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day; GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`**; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still builds.
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Windows PC; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the other two no later than Day 372; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` carries its comment convention (set on Day 346) for every addition and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated; the ABI snapshot (`abi/engine_api.snapshot`) is regenerated and the version rule of the spec is followed.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); a new C test host gets its `std_usage_check` allow-list entry.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the phase's scene tests pass on the Windows PC on both backends, or each failure is recorded with its observed behavior.

## Outcome

Done 2026-11-16 on the Windows PC.

- **The scene hosts on Windows.** `host_scene.c` (Days 359-368) and `walk_around.c` (Day 371) reach their window through
  `tests/engine_library/host_window.h`. The day first wrote that layer as a header of its own; the Mac's Day 370 had pushed the same layer meanwhile
  (implementation files per platform, an opaque `host_window`, a posted key), so at the rebase the day took the Mac's and added
  `windows/host_window_win32.c` (a window class of the host's, borderless windows of the exact client size, the message loop turned by the polls, the key
  posted as `WM_KEYDOWN`) and `host_window_api_flags`. `HOST_SURFACE_API` chooses the graphics API: `engine_c_scene_d3d12`, `engine_c_scene_vulkan`, `engine_c_walk_around_d3d12` and `engine_c_walk_around_vulkan`, plain and asan, all pass and all
  ran (none skipped). Spikes 1 and 2 (`scene_spikes_test.cpp`) open a Win32 window on Windows; with `ENGINE_RHI_REQUIRE_GPU=1` the five spikes pass
  there (they make Vulkan devices, as Day 358 measured).
- **Golden images.** Direct3D 12 and Vulkan draw the golden scene byte for byte the same on the PC (AMD Radeon 860M); both differ from the Linux golden
  (NVIDIA, Vulkan) by at most 2, 40 of 76 800 values by more than 1, 523 values at all: inside the rule of Day 358, with no change to the tolerance.
- **Found and fixed: one D3D12 device per adapter.** `D3D12CreateDevice` returns the adapter's existing `ID3D12Device`, so the surfaces of one GPU share
  it (verified by pointer). Destroying the second surface reported the first one's objects as leaks (about 175 warnings): the RHI now counts the users of
  a D3D12 device and only the last reports (`dx12_device.cpp`; test `Two DX12 devices on one adapter share the D3D12 device, ...`, shown to fail without
  the fix).
- **Found and fixed: recovery from a lost device.** The test extension gained `surface_lose_device` (the RHI's `InjectDeviceLoss` on a surface's device),
  and `host_scene.c` loses one surface's device, then one of two surfaces', on both backends. That found three defects: (1) `surface_rebind` reused the
  lost device, so a lost surface could never recover: it now makes a new device; (2) the new device was picked anew, and on Direct3D 12, while another
  surface still held the removed device, the hardware adapter read as unsuitable and the choice fell silently to WARP (seen in the log, a slightly
  different picture): a surface now keeps the adapter of its first device and a recovery asks for it, answering `UNSUPPORTED` until no surface holds the
  removed device; (3) a refused rebind unbound the world for good, and the next rebind that worked reported OK with nothing bound: the world is kept
  and drawn again. Each was shown failing by the test before its fix. On Vulkan only the lost surface is lost; on Direct3D 12 every surface of the
  adapter is.
- **The Win32 C host:** `walk_around.c` runs on Windows with its own window and message loop, forwarding keys and the mouse (the cursor and clipboard
  callbacks of a Win32 host are Day 354's `host_win32.c`).
- **Not done:** the five-thousand-frame and fault-injection items of Day 368 (still closing-pass items, `KNOWN-ISSUES.md`); the Linux and macOS
  builds of the merged hosts and CMake are not compiled here (the Linux machine and the Mac build them, `KNOWN-ISSUES.md`).
- **After the rebase onto Day 370** (`009a77c8`, and `178453a6`): `build-msvc` builds clean, and `engine_c_scene_d3d12`, `engine_c_scene_vulkan`,
  `engine_c_walk_around_d3d12` and `engine_c_walk_around_vulkan`, plain and asan, pass (8 of 8), with the Mac's posted-W-key check passing on Windows. All six trees build clean; `ctest -L engine_library` 99 of 99 in `build-msvc` and `build-msvc-shared`, 86 of 86 in `build-windows-clang-cl-nounity` and `build-clang-cl-shared`; the gates 57 of 57; `ctest -L docs` 7 of 7.

Runs: `scripts/test-phase.sh --affected` (every phase: an `engine/foundation`-free change set, but `tests/engine_library/CMakeLists.txt` and the RHI
changed): `build-msvc` `ran 3163 of 6085 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 320 s`, `build-windows-msvc-nounity` `ran 3163 of 6085 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 322 s`, `build-clang-cl` `ran 3193 of 3349 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 316 s`,
`build-windows-clang-cl-nounity` `ran 3193 of 3349 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 323 s`; asan: `build-msvc` `ran 2639 of 6085 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 196 s`, `build-windows-msvc-nounity` `ran 2639 of 6085 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 180 s`.
Failures: `Throughput scales with participant count` once (`build-msvc`) and four DX12 tests (one per run in the non-unity trees and the non-unity asan
run), each failing at `D3D12CreateDevice` with `DXGI_ERROR_DEVICE_REMOVED` (the filed flake); every one passed 3 of 3 reruns, and the DX12 tests run three
times in parallel with the scene tests' deliberate losses passed 50 of 50 each time. Shared trees: `ctest -L engine_library` 97 of 97 (`build-msvc-shared`)
and 85 of 85 (`build-clang-cl-shared`). The gates pass; `ctest -L docs` 7 of 7. All six trees build clean. After the review's last change (the dead check
removed) the D3D12 and scene tests passed 100 of 100 in `build-msvc`.

## Post-implementation code review

`/code-review high` over the day's diff, done inline: 8 findings. **Fixed (5):** the four defects the tests found (the shared device's false leak
report, the reused lost device, the silent WARP fallback, the dropped world), each with a test shown failing first; and a check in `Dx12Device::Initialize`
that refused a removed device handed back by `D3D12CreateDevice`, whose comment claimed D3D12 does that: the runs showed the adapter's suitability test
fails instead, so the check never fired (removed). **Recorded, not fixed (3):** each `Dx12Device` registers its own callback on the shared device's
info queue, so a debug message reaches every wrapper of the adapter and is counted once per wrapper (`KNOWN-ISSUES.md`); a surface pinned to its first
adapter cannot follow a GPU that is physically gone (by the no-silent-fallback rule; the host destroys and recreates the surface to pick another); the
loss hook runs the RHI's loss path, which logs, under the surface mutex (test only; the log callback's re-entrancy limit of Phase 23).

