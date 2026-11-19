# Shorter test runs / Day 4: short forms of the long loops; the slow gates

**Series:** [shorter test runs](../test-time.md)
**Depends on:** Day 3 (the label and the measured list).

## Re-planned on 2026-11-19, after measuring each test alone

Day 3's list came from a full run, where ctest ran about 32 tests at once, dozens of them on the one GPU. Run alone, one at a time
(`ctest -j1`, plain unless named):

| Alone (s) | In the full run (s) | Test |
|---|---|---|
| 60.6 / 60.2 | 61.0 / 60.4 | Hostile scripts for a minute [asan] / [tsan] (plain: 10.0) |
| 46.3 / 42.6 | 99.5 / 88.4 | `engine_c_script_sample_tsan` / `_asan` (plain: 6.7) |
| 43.0 | 56-76 | `test_phase_labels` |
| 28.5 | 40-52 | `platform_neutral_check` |
| 22.5 / 22.4 | 38-54 / 32-43 | `script_demo_window_x11` / `_wayland` |
| 17.3 / 16.3 | 32 / - | `file_access_check` / `std_usage_check` |
| 16.5 | 78-177 | Resets in a loop on a real device |
| 15.7 / 14.5 | 36 / 35 | `shaders_check` / `shaders_portability` |
| 3.9 | 61-91 | Device allocation failures at any point |
| 3.2 | 59-79 | The pipeline cache is written at shutdown |
| 3.0 | 56-64 | Repeated losses on a real device |
| 2.4 | 55-59 | Vulkan devices are destroyed and recreated |
| 1.9 | 40-42 | `engine_c_scene` |
| 1.3 | 32-41 | Losing the real device in the deferred pipeline |
| 1.0 / 1.1 | 33 / 32 | The Vulkan backend conforms to the device lifecycle; Bind group misuse is refused |

**The assumption was wrong.** Most of the GPU tests are not long loops: they take 1-4 s alone, and 30-90 s only while dozens of others
share the GPU. A short form would save nothing there. The tasks change:
- **Short forms** only for tests that are long alone: the hostile-script minute (a duration), the reset loop (100 resets) and the C
  scripting sample's sanitizer runs.
- **New: GPU contention, measured.** Run the plain suite with the GPU tests limited to a few at a time (a ctest resource), and compare
  the wall time and the GPU tests' times with today's. Adopt it only if the wall time does not grow; the user decides with the numbers.
- **The slow gates** stay as planned (`test_phase_labels`, `platform_neutral_check`, and the 15-17 s ones).
- The window tests (`script_demo_window_*`, 22 s alone) are a fixed number of frames on a window; they stay as they are (under the bar
  alone).

## Tasks

- [x] **Short forms of the long loops** (from Day 3's list):
  - Resets in a loop on a real device;
  - Device allocation failures at any point;
  - Repeated losses on a real device;
  - The pipeline cache is written at shutdown;
  - Vulkan devices are destroyed and recreated;
  - Hostile scripts for a minute;
  - the `engine_c_script_sample` sanitizer runs;
  - and any others on the list.
  - Each long loop becomes one test body with its count (or duration) as a parameter. The ordinary test runs a short count; a second
    registration with the full count carries `soak`.
  - The checks are identical in both. Only the iteration count or duration differs, so no check is lost.
  - A short form's count is chosen so it still exercises every path it did before. Example: an allocation-failure sweep still fails at every
    allocation point, but with fewer frames per point. Write down for each test why its count is enough.
  - Target: no ordinary test over 30 s in the plain flavor, or over 60 s in asan and tsan.
- [x] **Prove the short forms still catch what they were for:**
  - for each reset or loss test, plant one leak (an extra device object kept) and check that the short form fails;
  - then remove the plant;
  - record each check in the Notes.
- [x] **The slow gates:**
  - profile `test_phase_labels` (76 s) and `platform_neutral_check` (52 s), and any other gate over 10 s;
  - find what costs, for example a `file(READ)` or a regex per file per rule in CMake script;
  - make the same checks faster: one read per file, rules combined, or a small C++ checker if CMake script cannot do it;
  - the gates' fixture tests must keep passing with the same findings;
  - target: each gate under 10 s.
- [x] **Docs:** `docs/guide/building-and-testing.md` ("Writing a test"): how to give a long test a short form and a soak form.
- [x] **Standing rows:**
  - `--affected` (plain), then `--affected --soak`;
  - asan and tsan of the same selection, since the reset, loss and allocation tests are lifetime code;
  - `/code-review high`;
  - `STATUS.md`, `KNOWN-ISSUES.md`;
  - commit and push.

**Done when:**
- no test outside `soak` takes over 30 s plain on Linux (Day 3's warning lists none);
- every shortened test is shown to fail on a planted leak;
- the slow gates are under 10 s with unchanged fixture results;
- the series' Done measurement (the old and the new run of the unlit change) is recorded in `../test-time.md`.

## Notes

### What was done (2026-11-19)

**Short forms** (`tests/support/soak.h`: `test_support::Soak()`, `SoakCount(short, full)` read `ENGINE_TEST_SOAK=1`;
`engine_soak_catch_test()` in `cmake/EngineTestPhases.cmake` registers `<case> [soak]` with the variable, the source file's phase, the
`soak` label, an optional suffix and flavor label, and its own environment):
- **Resets in a loop on a real device:** 20 resets (sanitized: 20, as before), 100 in its soak form (plain, its own pipeline-cache
  directory). 16.5 s alone becomes 4.0 s; the soak form takes 21.8 s.
  - Why 20 is enough: every reset compares the device's object counts and the registry. The memory drift is read over resets 5-9 and
    the last five, which still do not overlap. A smaller per-reset memory leak is what the 100-reset soak form sees.
  - **Planted leak:** with one buffer kept per reset, the 20-reset form fails at its teardown checks (`LeakedObjects() == 0`, lines 188
    and 190), in 4.3 s. The per-reset count check did not catch it; recorded as observed.
  - The Mac's `metal_autorelease_pools`, the only run of the loop's memory check on Metal (no validation layer), sets
    `ENGINE_TEST_SOAK=1` and keeps its 100 resets (review finding 1).
- **Hostile scripts:** ten seconds in every flavor (sanitized: 60 s before), a minute in each flavor's soak form. Renamed "... at once,
  for ten seconds (a minute in soak), and the world goes on" (review finding 3). Its checks hold from three ticks, and ten seconds is
  hundreds plain and dozens sanitized; no plant was made, since no check depends on the count.
- **Not shortened:**
  - the GPU tests that are short alone (above);
  - `engine_c_script_sample_asan` and `_tsan`, a shipped sample whose frame count is its own, 43-46 s alone and under the 60 s sanitizer
    bar. Under load the tsan one reached 61.2 s once and was named by the warning; recorded, not changed;
  - the script-demo window tests (22 s alone).

**The slow gates:**
- `test_phase_labels`: 43.0 s, now 3.2 s.
  - It listed the tests about 140 times (`ctest -N` per phase and per phase and flavor, 0.4 s each) and found duplicates with a quadratic
    `list(FIND)`.
  - It now reads one `ctest -N -V` listing and counts per test. Its 12 fixture cases pass unchanged.
- `platform_neutral_check`: 28.5 s, now 6.1 s.
  - Rules 2, 3, 4 and 6 walked every line of every file in CMake. Each now first asks `file(STRINGS REGEX ... LIMIT_COUNT 1)` whether
    any raw line holds the rule's distinctive token, with no anchor, and skips a file with none.
  - Stripping comments only removes text, so the filter cannot hide a violation, and a file it lets through is walked as before.
  - A new fixture, `platform_neutral_check_comment_prefix`, plants violations behind a leading block comment (`/* note */ #include
    <ft2build.h>`) and one with a non-ASCII character in that comment.
  - With rule 6's filter made to match nothing, `platform_neutral_check_raw_defines` fails (checked, then restored).
- `file_access_check`: 17.3 s, now 8.75 s, by the same filter. With the filter made to match nothing, 3 of its 7 fixture cases fail
  (checked, then restored).
- Not changed: `std_usage_check` (16.3 s; it already filters each whole file), `shaders_check` and `shaders_portability` (15.7 s and
  14.5 s, `slangc` itself).

**GPU contention: not tried.**
- The GPU tests overlap the window tests' serial chain, which set the plain suite's wall time in both runs: 125 `display`-locked tests,
  115 s on Day 3 and 205 s today, while the desktop was in use (Steam started, a remote-desktop window open).
- Limiting GPU concurrency would not shorten that chain, so the measurement was dropped.
- The window tests are the next lever, a question for the user.

### Runs

- `scripts/test-phase.sh --affected` (every phase, since `cmake/` changed): `ran 4272 of 11472 tests (phases: all; flavors: plain;
  display: included; crash tests: excluded; soak: excluded) in 206 s`.
  - The total test time fell from 2,294 s to 1,742 s. The wall time is the display chain's 205 s (Day 3: 115 s), with the desktop in use.
  - Failures: `engine_c_surface`, the Job-destroyed test and the voice-cost snapshot (all known); and two X11 window tests,
    `X11Window takes the pointer grab ...` and `X11Window::Create opens a mapped window ...`. Those two failed with the desktop in use
    (`KNOWN-ISSUES.md`).
  - The only warning: `script_demo_window_wayland`, 50.8 s, in the display chain.
- Phases 8 and 27 in every flavor (the reset loop and the hostile scripts are lifetime and threading tests):
  - phase 8: `ran 367 of 11472 tests (phases: 8; flavors: all; display: excluded; ...) in 56 s`, no failures;
  - phase 27: `ran 930 of 11472 tests (phases: 27; flavors: all; display: excluded; ...) in 80 s`. The one failure is the known tsan LuaJIT
    sandbox flake.
- The soak forms: the reset loop 21.8 s; the hostile scripts 60.0 s plain, 60.6 s asan and 60.2 s tsan; all pass.
- Builds: Clang unity (`build`) and GCC non-unity (`build-gcc`, `rhi_tests` and `script_tests`) clean. The GCC build found that
  `soak.h` included `os/os.h`, where `os::GetEnvironment` is not declared (`os/console.h`); the unity build had hidden it. Fixed.
- After the review's fixes: the renamed hostile forms pass (10.0 s and the three 60 s soak forms); `test_phase_labels` and its 12
  fixtures, `platform_neutral_check` and its 5 fixtures, and `file_access_check` and its 6 fixtures pass.

### Post-implementation code review

`/code-review high`: 3 findings; 2 fixed, 1 not reproducible.
1. The Mac's `metal_autorelease_pools` runs the reset case directly, and would have dropped to 20 resets. It is the only run of the
   memory check on Metal, so it now sets `ENGINE_TEST_SOAK=1`. Not run here: it is a Mac test.
2. `file(STRINGS)` without `ENCODING UTF-8` might split a line at non-ASCII bytes and let a filter miss a violation. A fixture line
   with a UTF-8 character still matched without the option on CMake 4.5, so the split does not happen here. `ENCODING UTF-8` was added to
   both filters anyway, and the fixture line kept.
3. The hostile-script case was still named "for a minute"; renamed.
