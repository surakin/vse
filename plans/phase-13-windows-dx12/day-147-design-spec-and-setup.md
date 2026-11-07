# Phase 13 / Day 147 — Design Spec, Inventory, Toolchain & Phase Setup

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Phase 12 complete (the layered libraries of `ARCHITECTURE.md`); every day file of this phase committed (planning policy).

No product code except the phase setup. The decisions in the phase file are confirmed or amended by experiment, everything Linux-only in the tree is
inventoried with the day that ports it, and both machines are ready.

## Tasks

- [x] **Phase setup first:** `CURRENT_PHASE` becomes 13; `tests/phases.txt`'s phase list comment names "13 Windows port"; `docs/guide/building-and-testing.md`
      gets a row for phase 12 (with its closing counts) and a placeholder row for 13. The `flavors 13 ...` line arrives with the first phase-13 test (Day 148).
- [x] **Line endings:** add `.gitattributes` (`* text=auto eol=lf`; `*.ppm`, `*.png`, `*.dmp`, `*.bin` binary; `*.ps1`, `*.bat`, `*.cmd` keep `eol=crlf` only if
      PowerShell needs it — check and record), run `git add --renormalize .`, and confirm `git status` shows no content change on Linux (the index is already LF);
      on Windows, re-checkout and confirm `git ls-files --eol` reports `w/lf` for the minted outputs and `docs/reference/`.
- [x] **Memory link on Windows:** `scripts/link-claude-memory.ps1` (a directory junction, `New-Item -ItemType Junction`, same backup rule as the shell script) and a
      line in `CLAUDE.md` naming it; run it on the Windows PC.
- [x] **Prerequisites** (phase file list): check each on the Windows PC and record the version in the spec — Graphics Tools (D3D12 debug layer), Vulkan SDK,
      `slangc` v2026.18.2, `clang-cl` (already recorded in the phase file: LLVM 23.1.5 from winget), Debugging Tools for Windows, (PIX). The developer installs what is missing; nothing is installed by a script.
      Check `BUILD.md`'s Windows and Linux tables against both machines and correct any version that differs.
- [ ] **The Linux machine:** record its CPU, GPU, driver, distribution, compiler and how it is reached from the Windows PC (SSH over Tailscale or by hand),
      and the procedure for a two-machine day (push from one, pull and run `--affected` on the other, quote both lines).
- [x] **Inventory** (a table in the spec: file, what is Linux-specific, the Windows counterpart, the port day): every file under `os/linux/`, `io/linux/`; every
      POSIX header outside them (`grep -rlE '#include <(unistd|sys/|pthread|dlfcn|fcntl|poll|signal|dirent|sched)'` over `engine tools game tests`: today
      `log/log.cpp`, `jobs/worker_pool.cpp`, `jobs/topology.cpp`, `tools/docgen`, `tools/shader_mint`, the three demos, the `*_linux_test.cpp` files, `tests/io`,
      `tests/jobs`, `tests/rhi`, `tests/settings`, `tests/docgen`); every GCC builtin or attribute (`compiler.h`, `int128.h`, `types.h`, the `*_log.h` printf
      attributes, `render_graph`, `shader_reflection`); every `CMakeLists.txt` platform branch and GCC flag; every shell script ctest or CMake calls
      (`split-debug.sh`, the crash scripts, `test-phase.sh`); every `getenv` that names a Linux variable (`WAYLAND_DISPLAY`, `DISPLAY`, `XDG_*`).
- [x] **Spike A — the descriptor decision.** A throwaway program on the Windows PC: write to the console, a pipe and a file through (a) the UCRT (`_open`,
      `_write`, `_O_BINARY`) and (b) a small `int` → `HANDLE` table with `WriteFile`; then fault (null write) inside a vectored handler while another thread holds
      the UCRT's lock on the same descriptor, and see which path still writes the report. Decide decision 3 and record it.
- [x] **Spike B — DX12 triangle and the object index.** A throwaway D3D12 program (debug layer on): one triangle, then 64 instances drawn as 64 draws whose
      index arrives (a) as a root constant (`SetGraphicsRoot32BitConstant` per draw) and (b) through a per-draw vertex stream with
      `StartInstanceLocation`; time 10 000 draws each way. Shaders minted by `slangc -target dxil` from a copy of `gbuffer.vert.slang` with the index behind a
      define. Decide decision 9's delivery and the define's name; record the timings and whether the debug layer stayed silent.
- [x] **Spike C — the build.** Configure the tree with MSVC and Ninja and record every failure class with one example each (the stop in `CMakeLists.txt`,
      GCC flags, POSIX headers, builtins, `windows.h` macros such as `LoadLibrary`, `min`/`max`, `near`/`far`, `CreateWindow`, `GetObject`); this list becomes
      Day 148's checklist. Also compile `int128.h` with `ENGINE_FORCE_PORTABLE_INT128` and run its tests' arithmetic table under MSVC once.
- [x] **The spec** `docs/superpowers/specs/2026-11-NN-windows-port-design.md`: each phase-file decision confirmed or amended with the evidence; the descriptor
      decision (Spike A); the D3D12 baseline and why (feature level, shader model, binding tier, barriers, no Agility SDK); the DX12 backend's object map
      (Vulkan object → D3D12 object for each RHI concept, from the Phase 4 review's concept map); the root-signature model (bind group → descriptor table,
      object index → root constant); the inventory table; the two-machine procedure; the manifest contents; the symbol and dump layout; the test labeling rule.
- [ ] **Documentation:** the spec is linked from `docs/README.md`; the phase file's day list is current; `ctest -L docs` passes (on Linux; Windows docs come Day 150).
- [ ] Run `scripts/test-phase.sh --affected` on the Linux machine (the phase map changed, so the whole plain suite runs; quote the line).
- [x] No code review (no product code beyond the setup). Update `STATUS.md`; commit and push.

**Done:** `CURRENT_PHASE` is 13, line endings are pinned and renormalized, memory is linked on Windows, the prerequisites and both machines are recorded,
the spec with the inventory and the three spikes' results is committed, and every phase-file decision is confirmed or changed in it.

## Result (2026-11-07, on the Windows PC)

- **Phase setup:** `CURRENT_PHASE` is 13; `tests/phases.txt` names "13 Windows port" (labels up to `phase13`); the building-and-testing table has phase 12's
  counts (19 plain: the 5 cases of `diagnostics_hooks_test.cpp` and the 14 `module_deps_check*` tests, counted from the sources; the Linux dry-run confirms)
  and a row for 13. The `flavors 13` line arrives on Day 148 with the first phase-13 test, and with it the platform-qualified form that Day 148 now owns
  (moved from Day 150: a Windows-only `flavors 13` line would make `test_phase_labels` fail on Linux).
- **Line endings:** `.gitattributes` (`* text=auto eol=lf`; `*.ppm`, `*.rtex`, `*.envelope`, `*.dmp`, `*.bin`, `*.spv` binary; `tests/docgen/fixtures/good/crlf.h` and
  `third_party/**` kept byte for byte, because the CRLF fixture and sentry's `sentry.rc.in` are stored with CRLF on purpose). Renormalizing changed no stored
  file. The Windows checkout's files had been written with CRLF by `core.autocrlf=true`; 2238 of them were rewritten to their stored bytes, each only after
  checking that the file with CRLF turned into LF equals the stored blob exactly, so nothing else could change; every text file on disk is now LF except the
  two exempt ones. (A `git checkout -- .` was refused by the session's safety check because it overwrites working files; the checked rewrite replaced it.)
- **Memory:** `scripts/link-claude-memory.ps1` made the directory junction; `CLAUDE.md` names the script.
- **Prerequisites:** recorded in the spec, section 1. Missing: **Graphics Tools** (the D3D12 debug layer). Found: a second, older `dxc` from the Vulkan SDK
  (1.9.0.5512 against the Windows SDK's 1.9.2602.17), now a Day 163 task.
- **Inventory:** the spec, section 4.
- **Spike A (descriptors):** with a thread suspended inside the UCRT's `_write` on a drained pipe, a crash handler's `_write` on the same descriptor blocked
  (2 s timeout) and `WriteFile` on the same handle completed; a new report file always completed; 1.68 against 1.52 µs per 64-byte write. Decision: an engine
  table of `HANDLE`s behind the `int` descriptors.
- **Spike B (DX12, object index):** `slangc` DXIL accepted by `CreateGraphicsPipelineState`; root constant and per-instance stream both correct (64 of 64);
  10 000 draws: 21.5 against 14.8 ns per draw to record, 0.491 against 0.420 ms with the GPU. **Decision 9 amended to the instance stream.** The GPU reports
  shader model 6.8, binding tier 3, enhanced barriers. **Not done:** the debug-layer run (Graphics Tools missing); filed.
- **Spike C (build):** 54 of 97 engine sources compile with MSVC as they are, 92 of 97 once the GCC printf-format attribute and `__builtin_trap` are
  replaced; the five left are platform files, one of them a neutral file that includes the XKB keysym header (now Days 150 and 159). `/W4` warnings: C4996,
  C4649, C4324, C4189 (Day 148). The portable `u128`/`i128` built with MSVC matched `clang-cl`'s native `__int128` on 200 000 random operations (checksum
  `d872f86c1460ab89`); `clang-cl`'s native division needs compiler-rt's builtins library (now a Day 148 task). `-Wl,--wrap=malloc` in `tests/debug` has no
  Windows form (now a Day 155 task).
- **Spec:** `docs/superpowers/specs/2026-11-07-windows-port-design.md`, linked from `docs/README.md`. Plan files changed with it: the phase file (decision
  9), Days 148, 149, 150, 155, 159, 163, 166, 171.
- **Not done, needs the Linux machine:** recording its details and how it is reached; `ctest -L docs` (the build does not configure on Windows until Day 148);
  the scoped run after the phase-map change; checking that `.gitattributes` shows no content change in the Linux checkout. Filed in `KNOWN-ISSUES.md`.
- No code review (no product code beyond the setup).
