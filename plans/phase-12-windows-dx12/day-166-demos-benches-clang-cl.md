# Phase 12 / Day 166 — Demos, Benchmarks and the clang-cl Build

**Phase:** [Phase 12 — Windows Port + DX12 Backend](../phase-12-windows-dx12.md)
**Depends on:** Day 165.

Everything the phase ships, exercised as a user would, measured, and built with the second compiler.

## Tasks

- [ ] **Demos on both backends:** `rhi_demo` and `renderer_demo` 1000 frames with `--validation` on DX12 and on Vulkan (exit 0, validation silent); every
      command-line option and setting the demos document; `--crash-handler` on and off; record each run.
- [ ] **Benchmarks:** `rhi_bench`, `renderer_bench` and the jobs benchmarks on Windows, DX12 and Vulkan, in a table in the spec beside the Linux numbers;
      the Phase 4 review's prediction (descriptor copy cost) checked and the conclusion written.
- [ ] **Release configuration:** a `Release` build of the demos on Windows (crash handler default off, PDBs still produced), stored in the symbol store, one
      deliberate crash read back through Day 149's scripts.
- [ ] **The `clang-cl` build, run in full:** every target builds with `clang-cl` (unity on and off) and the full plain suite runs on it once; record the result and
      any test that behaves differently; the plan's rule stays "builds every day, full run at the close".
- [ ] **Unity off:** `-DENGINE_UNITY_BUILD=OFF` builds every target on Windows with MSVC (CLAUDE.md rule) and the plain suite passes.
- [ ] **Interactive checks** (record or file): the demo's orbit, fly, mouse drag and wheel controls with real input on Windows (the same item is open for Linux
      in `KNOWN-ISSUES.md`; close the Windows half only).
- [ ] **Runs:** Windows `--affected` plain and asan; Linux `--affected` if anything shared changed; quote the lines.
- [ ] **End-of-day review:** `/code-review high` over the day's diff (fixes found while running the demos, benchmark code).
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** both demos run on both backends with validation silent, the benchmark table is recorded, the Release build's crash is readable from the symbol store, and
the `clang-cl` and non-unity builds pass.
