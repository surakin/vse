# Maintenance: shorter test runs

Not a numbered phase. The user's request (2026-11-19): "there are a lot of tests right now, and they take a really long time to run after every
big change. What can be done about that?" They asked for this plan after the measurement below.

## What a run costs now (measured on Linux, 2026-11-19)

The run: the unlit-materials change (`258cc983`), `scripts/test-phase.sh --affected --flavor all`, 32 CPUs.

- **Total: 902 s.**
  - **Build, about 450 s.** The script builds with `cmake --build build` and no `-j`. The tree is Unix Makefiles with
    `CMAKE_BUILD_PARALLEL_LEVEL` unset, so the build may run one job at a time. It also builds every flavor's targets whatever `--flavor` says.
  - **ctest: 452 s of wall time** (`Total Test time (real) = 451.61 sec`). That is 6,800 s of test time over 10,927 tests.
- **One test sets the wall time:**

  | Test | Seconds |
  |---|---|
  | `audio_bench_large` | 450 |
  | `physics_bench_large` | 277 |
  | `script_stress_large` | 262 |
  | Resets in a loop on a real device (plain) | 177 |
  | `engine_c_script_sample_tsan` / `_asan` | 100 / 88 |
  | Device allocation failures at any point (plain / asan) | 91 / 77 |
  | `test_phase_labels` | 76 |
  | `platform_neutral_check` | 52 |

- **The long tail:**
  - the 10 slowest tests are 25% of the test time, and the 50 slowest are 47%;
  - 9,255 of the 10,927 took under 0.1 s, so the number of tests is not the problem.
- **`--affected` ran everything.** It compares with the last commit that changed `CURRENT_PHASE`. During the long stretch of maintenance work
  since then, that diff has gathered root `CMakeLists.txt`, `CMakePresets.json` and foundation-header edits, so every run widens to every phase.
  The unlit change itself touched only the renderer, the importer, two shaders and their tests.

## The aim

A typical day's scoped run goes from about 15 minutes to a few minutes on Linux, and nothing is lost:
- every test still runs at a phase's close (`--all --flavor all`), and on demand;
- a reduced run still says exactly what it left out (the `ran X of Y` line).

**Not wanted:**
- deleting tests, or weakening what a test checks to make it faster;
- skipping a test silently;
- a different test framework.

## Decisions (proposed; confirmed by the user, 2026-11-19: "Confirm, start Day 1")

1. **The `--affected` base:**
   - the commit recorded when the day starts (`scripts/test-phase.sh --start-day` writes it to `.git/test-phase-base`);
   - else the merge-base with the upstream branch;
   - else, as today, the `CURRENT_PHASE` commit.
   - The script prints the base it used. `--since` still overrides it.
2. **A `soak` label** for the long runs: the three `*_large` benches, and the long loops of the GPU reset, device-loss, allocation-failure and
   hostile-script tests.
   - It is left out of `--current`, `--affected` and a single phase, like `crash`.
   - It is included in `--all` and with `--soak`.
   - The bar is a test that takes over 30 s in the plain flavor on the Linux machine.
3. **Long loops get a short form in the ordinary run, and the full length in soak.** Each one stays the same test with the same checks, run at
   a lower count. Benches stay soak-only.
4. **The build step:**
   - builds in parallel (`--jobs`);
   - builds only the targets of the chosen flavors, through one aggregate target per flavor (`engine_tests_plain`, `_asan`, `_tsan`).
5. **Ninja and sccache are measured, not assumed.**
   - Each is adopted only if it shortens a no-op build and the unlit-sized rebuild, on Linux.
   - Adopting one is a `BUILD.md` change, and a presets change for Ninja.
6. **The sanitizer flavors stay as CLAUDE.md has them:** asan and tsan only on days that touch concurrency, lifetime or memory code.
   - The script's summary line names the flavors, so a plain-only run is visible.
   - Nothing is automated here: it is a rule for the person running the tests.

## Days

1. [Day 1: the build step: parallel, and only the chosen flavors](test-time/01-build-step.md)
2. [Day 2: the `--affected` base](test-time/02-affected-base.md)
3. [Day 3: the `soak` label](test-time/03-soak-label.md)
4. [Day 4: short forms of the long loops; the slow gates](test-time/04-long-loops-and-gates.md)
5. [Day 5 (the PC and the Mac, the user's machines): the script and labels there; their numbers](test-time/05-windows-macos.md)

**Done when** the unlit change's run is measured again with the new script on Linux, and both numbers are recorded below:
- **the old run:** `--since 258cc983^ --flavor all`;
- **the new run:** `--affected` with the day base at `258cc983^`, in the flavors CLAUDE.md asks for (plain).

The full `--all --flavor all` run still includes every test, `soak` included, and its count equals today's.

## Notes

(Filled in as the days are done.)

## Addendum (2026-11-19): the window tests

After Day 4 the plain suite's wall time is the window tests' serial chain (125 tests holding the `display` lock, 115 s on a quiet desktop).
70 s of it is `script_demo_window_x11` and `_wayland` (1,000 frames each, at the display's rate), and about 45 s is the X11 halves of six
demo pairs. The window layer (101 tests) and the RHI presentation tests (12) test each backend's own code; the demos run the same code on
both, above the window.

**Decision (the user, 2026-11-19, "1"):**
- the demos run on Wayland only in ordinary runs. Their X11 copies (`script_demo_window_x11`, `model_viewer_window_x11`,
  `ui_demo_script_x11`, `renderer_demo_script_x11`, `renderer_demo_overlay_x11`, `input_probe_runs_x11`) carry `soak`, so `--all` and
  `--soak` still run them;
- `script_demo_window_*` draws 120 frames; a `..._long` copy with 1,000 frames carries `soak`.
- The window layer's tests and the RHI presentation tests keep both backends.

- [x] The six X11 demo copies labelled `soak`.
- [x] `script_demo_window_*` at 120 frames, with `script_demo_window_*_long` (1,000 frames, `soak`).
- [x] The display chain measured again; the soak copies run once; the review; `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done (2026-11-19):**
- Six X11 demo copies carry `soak`, in each demo's `CMakeLists.txt`. `script_demo_windowed` takes a frame count:
  - `script_demo_window_{wayland,x11,win32,cocoa}` draw 120 frames;
  - `..._long` draw 1,000 and carry `soak`.
- **Ordinary window tests:** 119 (was 125) on Linux, with the display included.
- **Measured** (`ctest -L display -LE soak`): 115 s, the same as Day 3, but not comparable.
  - `script_demo_window_wayland` fell from 31.7 s to 6.3 s, and the X11 copies left the chain.
  - Two Wayland presentation tests grew while the desktop was in use (load 4.8, Steam running): the Wayland frame loop from 2.2 s to
    18.5 s, and `model_viewer_window_wayland` from 1.6 s to 15.4 s.
  - From Day 3's quiet times, the chain would be about 45-50 s; not measured on a quiet desktop.
- **The soak copies:** all pass. `script_demo_window_wayland_long` 50.8 s, `_x11_long` 22.3 s, `script_demo_window_x11` 2.7 s,
  `model_viewer_window_x11` 5.4 s, and the other four about 1 s each.
- **The two X11 window tests** (`X11Window takes the pointer grab ...`, `X11Window::Create opens a mapped window ...`) failed again, with
  the desktop in use (`KNOWN-ISSUES.md`).
- **Review** (`/code-review high`): 1 finding, fixed. The comment on the X11 copies was a 190-column line pasted five times; it is now a
  short pointer to the guide, which holds the reason.
