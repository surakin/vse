# Maintenance / linux-known-issues / Day 06 — Failing and flaky tests seen on Linux

**Series:** [Known issues Linux can fix](../linux-known-issues.md)

## Tasks

- [ ] `scripts/test-phase.sh --start-day` at the start; scoped runs (`--affected` plain, then asan and tsan when the day touches concurrency or lifetimes) at the end, quoting the script's `ran X of Y` line.
- [ ] `A Job destroyed without being released frees its never-started frame` (`tests/jobs/job_test.cpp:121`): find why `Live()` is 0 in the Release tree (the frame is freed earlier, or the counter is compiled out) and fix the code or the test's claim; test passes in plain Release, Debug, asan and tsan.
- [ ] `engine_c_surface` in a Release tree (`tests/engine_library/host_surface.c:326`, `g_opens_from_program == 1`): find the cause (an assert-only call, an inlined open) and fix it; passes in Release and Debug.
- [ ] `Snapshot: the cost of a voice ...` (`tests/audio/snapshot_test.cpp:565`) in a Debug tree and under load: measure the cost in operations (voices mixed, samples written) instead of wall time, or compare against a calibration run in the same process; keep a wall-time form as a `soak` test.
- [ ] `test_phase_script_long_warning` (a regex for the long-test warning): reproduce under `-j` load, find why the warning was not found, fix the script's output or the test.
- [ ] `LastSaveStatus can be read while another thread saves` and `Benchmark: the wrappers cost what the standard types cost`: make them deterministic (a latch instead of a sleep; operation counts or a calibration ratio with a wide margin) and run each 200 times alone and under `-j16` load.
- [ ] The tsan `LuaJIT sandbox: a trusted state refuses instruction and time budgets ...` race in `btree_insert` of libgcc's unwinder: reproduce, decide whether it is a race in our code (the exception-less build registering frames) or a tsan false positive; fix, or suppress it with a reason in the tsan suppressions file the build already has, and delete the entry.
- [ ] UBSan in the vendored JPEG decoder (`third_party/stb_image/stb_image.h:2563` and `:2577`): narrow down the input in `asset_tests_asan "[gltf]"` that triggers it; patch the vendored file with the signed overflow made unsigned and record it in `third_party/stb_image/VENDORED.md`; the asan run prints no runtime error.
- [ ] `engine_c_script_sample_asan` and `_tsan` over the 60 s bar: a short form (fewer ticks) for the sanitizer flavors and the long form labelled `soak`; the script's warning is gone.
- [ ] `Hostile scripts: every kind of misbehavior at once ...` short form on a slow machine: make the test's claim independent of the machine (the number of ticks asked for is the number a time budget allows, at least 1, and the claims about errors and distinct kinds stay); verify on Linux with `taskset` to two CPUs; the Mac's run stays the user's.
- [ ] Run each fixed test 50 times in its flavors and record the counts in the day file.
- [ ] Both compilers, unity on and off; public API documented (`docgen lint` zero, `docs` target, the guide pages); `/code-review high` on the day's diff, findings verified, fixed with a regression test or declined with a reason, recorded below.
- [ ] `KNOWN-ISSUES.md` entries removed or rewritten, `STATUS.md` updated; commit and push.

**Done:** Every failing or flaky test above passes 50 of 50 in its flavors on Linux, or has an entry that says what was found and why it stays; the entries are removed or rewritten.

## Post-implementation code review

(To be filled in when the day is done.)
