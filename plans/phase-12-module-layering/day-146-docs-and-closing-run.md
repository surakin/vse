# Phase 12 / Day 146 — Docs, the Measured Graph, Closing Full Run & Wrap-up

**Phase:** [Phase 12 — Module Layering](../phase-12-module-layering.md)
**Depends on:** Days 140-145.

Make the documents describe the new structure, run everything, review the whole phase, close it.

## Tasks

- [x] **`ARCHITECTURE.md`:** section 2 replaced by the measured graph after the phase (from `module_deps_check`'s output): no cycle, the two RHI entries left for
      Phase 13 Day 162; section 3 marks each item done with the day; section 1 names every library.
- [x] **`PLAN.md`:** the architecture diagram and repository layout match (`engine/platform/` holds windows and input; the foundation list no longer claims them);
      assumption #11's interface notes point at `engine/platform/`; roadmap row; progress-log entry.
- [x] **Guides:** `getting-started.md`, `building-and-testing.md`, `platform-os.md`, `platform-windowing-and-input.md`, `foundation-logging-and-assert.md`,
      `foundation-debugging.md`, `porting-debugging.md` read end to end and corrected for moved paths, the hooks and the libraries; every tagged sample compiles and
      runs; `docgen lint` zero; reference regenerated and committed.
- [x] **Phase 13 plan check:** every path, library name and "Depends on" in `plans/phase-13-windows-dx12*` matches what this phase built (the Windows days build
      `foundation_base` first, then `foundation_io`, `foundation_debug`, `foundation_settings`, `platform`); correct any that drifted.
- [x] **Known-issues sweep:** the allow-listed RHI edges filed against Phase 13 Day 162; anything a day left; entries this phase fixed removed.
- [x] **Closing full run (CLAUDE.md, required for a phase's last day), on the Linux machine:** `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, both
      demos 1000 frames with `--validation` on Wayland and X11; record the final `ran X of Y tests (phases ...)` line here; nothing left out.
- [x] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (from the commit before Day 140 to the
      working tree); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [x] Update `plans/phase-12-module-layering.md` (status complete, days done), `STATUS.md`; `CURRENT_PHASE` stays 12 until Phase 13 starts (its day files exist);
      commit and push.

**Done (2026-11-07):** the code, `ARCHITECTURE.md`, `PLAN.md` and the guides describe the same layered structure, `module_deps_check` passes with only the two Phase 13 entries,
the closing line `ran X of Y tests (phases: all; flavors: all)` is recorded with every test passing or a filed known issue, and Phase 12 is marked complete.

## Result

- `ARCHITECTURE.md`: section 2 is the measured graph after the phase (136 module edges, 0 problems, the two RHI allow-list entries for Phase 13 Day 162, the library link graph); section 3 marks every item done with its day.
- `PLAN.md`: roadmap row 12 complete, repository layout (the foundation's four libraries, `/platform` holds windows and input), assumption #11 points at `engine/platform/`, progress-log entry.
- Guides: stale path in `platform-windowing-and-input.md` fixed; the logging, debugging, settings and building guides already carried the hooks, the retired `LoadConfig` and the libraries; `log.h` comments that still
  spoke of a config file now name the settings keys. Reference regenerated (4037/4037 documented), `ctest -L docs` 6 of 6.
- Phase 13 plan check: Days 148, 149, 158, 159 already name `foundation_base`, `engine/platform/` and the `platform` library; the `os/windows/` and `io/windows/` paths they use still exist. Nothing drifted.
- Known issues: the two Wayland `renderer_demo` entries stay open (not caused by this phase); the RHI edges are in the allow list for Phase 13 Day 162; nothing else was left.
- Closing full run: `scripts/test-phase.sh --all --flavor all` -> `ran 3488 of 3488 tests (phases: all; flavors: all; display: included; crash tests: included) in 103 s`; the only failures are `renderer_demo_script_wayland`
  and `renderer_demo_crash_after_reset` (the known NVIDIA/Wayland segfault in `KNOWN-ISSUES.md`, present before Day 140); skipped tests are the ones that need tools or settings this machine lacks. After the last comment
  edit (`log.h`) the plain suite was run again: `ran 1527 of 3488` with the same two failures, `ctest -L docs` 6 of 6.
- Demos, 1000 frames with `--validation`, exit 0 and no validation message: `rhi_demo` on X11 and on Wayland, `renderer_demo` on X11. `renderer_demo --backend wayland` segfaults on its first frame (the known issue), so that
  combination is not verified.

## Post-implementation code review

End-of-phase review (`/code-review high --max-findings all`), done inline over the phase diff (`0ba6f7c` to the working tree: 177 files, mostly renames with include changes; the code with logic is the hooks, `log.cpp`,
`os::EnvironmentEntry` and its one caller, `PlatformWindowHost`, the libraries' CMake, the gate). Count: 2 findings in the final pass, both fixed: `log.h` still said channels are overridable from a config file and that the name is
used in config files (the file form was retired). Earlier days' reviews found: Day 141 (the gate passed on a layerless module), Day 142 (tests lost assert traces once `log` stopped including `debug`; fixed with
`install_hooks.cpp`), Day 144 (missing ctor/dtor docs on the neutral host), Day 145 (three non-unity include gaps, a stale comment). Checked and found fine: the atomics of the hook slots (acq/rel, static-duration tables),
the `EnvironmentSnapshot::Capture` retry for entries over 1 KiB, `PlatformWindowHost::Open` after a failed `Open`, destruction order of `Impl`, the aggregate's link order. Declined: none.
