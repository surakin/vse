# Phase 13 / Day 147 — Design Spec, Inventory, Toolchain & Phase Setup

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Phase 12 complete (the layered libraries of `ARCHITECTURE.md`); every day file of this phase committed (planning policy).

No product code except the phase setup. The decisions in the phase file are confirmed or amended by experiment, everything Linux-only in the tree is
inventoried with the day that ports it, and both machines are ready.

## Tasks

- [ ] **Phase setup first:** `CURRENT_PHASE` becomes 13; `tests/phases.txt`'s phase list comment names "13 Windows port"; `docs/guide/building-and-testing.md`
      gets a row for phase 12 (with its closing counts) and a placeholder row for 13. The `flavors 13 ...` line arrives with the first phase-12 test (Day 149).
- [ ] **Line endings:** add `.gitattributes` (`* text=auto eol=lf`; `*.ppm`, `*.png`, `*.dmp`, `*.bin` binary; `*.ps1`, `*.bat`, `*.cmd` keep `eol=crlf` only if
      PowerShell needs it — check and record), run `git add --renormalize .`, and confirm `git status` shows no content change on Linux (the index is already LF);
      on Windows, re-checkout and confirm `git ls-files --eol` reports `w/lf` for the minted outputs and `docs/reference/`.
- [ ] **Memory link on Windows:** `scripts/link-claude-memory.ps1` (a directory junction, `New-Item -ItemType Junction`, same backup rule as the shell script) and a
      line in `CLAUDE.md` naming it; run it on the Windows PC.
- [ ] **Prerequisites** (phase file list): check each on the Windows PC and record the version in the spec — Graphics Tools (D3D12 debug layer), Vulkan SDK,
      `slangc` v2026.18.2, `clang-cl` (already recorded in the phase file: LLVM 23.1.5 from winget), Debugging Tools for Windows, (PIX). The developer installs what is missing; nothing is installed by a script.
      Check `BUILD.md`'s Windows and Linux tables against both machines and correct any version that differs.
- [ ] **The Linux machine:** record its CPU, GPU, driver, distribution, compiler and how it is reached from the Windows PC (SSH over Tailscale or by hand),
      and the procedure for a two-machine day (push from one, pull and run `--affected` on the other, quote both lines).
- [ ] **Inventory** (a table in the spec: file, what is Linux-specific, the Windows counterpart, the port day): every file under `os/linux/`, `io/linux/`; every
      POSIX header outside them (`grep -rlE '#include <(unistd|sys/|pthread|dlfcn|fcntl|poll|signal|dirent|sched)'` over `engine tools game tests`: today
      `log/log.cpp`, `jobs/worker_pool.cpp`, `jobs/topology.cpp`, `tools/docgen`, `tools/shader_mint`, the three demos, the `*_linux_test.cpp` files, `tests/io`,
      `tests/jobs`, `tests/rhi`, `tests/settings`, `tests/docgen`); every GCC builtin or attribute (`compiler.h`, `int128.h`, `types.h`, the `*_log.h` printf
      attributes, `render_graph`, `shader_reflection`); every `CMakeLists.txt` platform branch and GCC flag; every shell script ctest or CMake calls
      (`split-debug.sh`, the crash scripts, `test-phase.sh`); every `getenv` that names a Linux variable (`WAYLAND_DISPLAY`, `DISPLAY`, `XDG_*`).
- [ ] **Spike A — the descriptor decision.** A throwaway program on the Windows PC: write to the console, a pipe and a file through (a) the UCRT (`_open`,
      `_write`, `_O_BINARY`) and (b) a small `int` → `HANDLE` table with `WriteFile`; then fault (null write) inside a vectored handler while another thread holds
      the UCRT's lock on the same descriptor, and see which path still writes the report. Decide decision 3 and record it.
- [ ] **Spike B — DX12 triangle and the object index.** A throwaway D3D12 program (debug layer on): one triangle, then 64 instances drawn as 64 draws whose
      index arrives (a) as a root constant (`SetGraphicsRoot32BitConstant` per draw) and (b) through a per-draw vertex stream with
      `StartInstanceLocation`; time 10 000 draws each way. Shaders minted by `slangc -target dxil` from a copy of `gbuffer.vert.slang` with the index behind a
      define. Decide decision 9's delivery and the define's name; record the timings and whether the debug layer stayed silent.
- [ ] **Spike C — the build.** Configure the tree with MSVC and Ninja and record every failure class with one example each (the stop in `CMakeLists.txt`,
      GCC flags, POSIX headers, builtins, `windows.h` macros such as `LoadLibrary`, `min`/`max`, `near`/`far`, `CreateWindow`, `GetObject`); this list becomes
      Day 148's checklist. Also compile `int128.h` with `ENGINE_FORCE_PORTABLE_INT128` and run its tests' arithmetic table under MSVC once.
- [ ] **The spec** `docs/superpowers/specs/2026-11-NN-windows-port-design.md`: each phase-file decision confirmed or amended with the evidence; the descriptor
      decision (Spike A); the D3D12 baseline and why (feature level, shader model, binding tier, barriers, no Agility SDK); the DX12 backend's object map
      (Vulkan object → D3D12 object for each RHI concept, from the Phase 4 review's concept map); the root-signature model (bind group → descriptor table,
      object index → root constant); the inventory table; the two-machine procedure; the manifest contents; the symbol and dump layout; the test labeling rule.
- [ ] **Documentation:** the spec is linked from `docs/README.md`; the phase file's day list is current; `ctest -L docs` passes (on Linux; Windows docs come Day 150).
- [ ] Run `scripts/test-phase.sh --affected` on the Linux machine (the phase map changed, so the whole plain suite runs; quote the line).
- [ ] No code review (no product code beyond the setup). Update `STATUS.md`; commit and push.

**Done:** `CURRENT_PHASE` is 13, line endings are pinned and renormalized, memory is linked on Windows, the prerequisites and both machines are recorded,
the spec with the inventory and the three spikes' results is committed, and every phase-file decision is confirmed or changed in it.
