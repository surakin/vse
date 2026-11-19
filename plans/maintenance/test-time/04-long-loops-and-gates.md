# Shorter test runs / Day 4: short forms of the long loops; the slow gates

**Series:** [shorter test runs](../test-time.md)
**Depends on:** Day 3 (the label and the measured list).

## Tasks

- [ ] **Short forms of the long loops** (from Day 3's list):
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
- [ ] **Prove the short forms still catch what they were for:**
  - for each reset or loss test, plant one leak (an extra device object kept) and check that the short form fails;
  - then remove the plant;
  - record each check in the Notes.
- [ ] **The slow gates:**
  - profile `test_phase_labels` (76 s) and `platform_neutral_check` (52 s), and any other gate over 10 s;
  - find what costs, for example a `file(READ)` or a regex per file per rule in CMake script;
  - make the same checks faster: one read per file, rules combined, or a small C++ checker if CMake script cannot do it;
  - the gates' fixture tests must keep passing with the same findings;
  - target: each gate under 10 s.
- [ ] **Docs:** `docs/guide/building-and-testing.md` ("Writing a test"): how to give a long test a short form and a soak form.
- [ ] **Standing rows:**
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
