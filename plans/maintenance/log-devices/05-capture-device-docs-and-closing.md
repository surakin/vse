# Log devices / Day 5: a capture device for tests, documentation and the closing runs

**Series:** [log devices](../log-devices.md)
**Depends on:** Days 1 to 4.

## Tasks

- [ ] **`CaptureLogDevice`** in `tests/support/` (test-only; a `Vector` of records under a `Mutex`; `Lines()`, `Clear()`), with its own test.
- [ ] **Migrate** two or three of the `SetWriteSinkForTesting` users that only read log lines (`tests/foundation/log_test.cpp`,
  `tests/ui/debug_overlay_test.cpp`) to the capture device; the assert and crash tests keep `SetWriteSinkForTesting` (they test fd 1).
- [ ] **Guide**: `docs/guide/foundation-logging-and-assert.md`, the narrative for log devices (what a device is, adding one, thread rules, why
  the crash path bypasses the table, an example of writing a device) with a compiling sample. Written here because it is the series' last day.
- [ ] **`PLAN.md`** progress log and assumption #10 note; **`STATUS.md`**; **`KNOWN-ISSUES.md`** (record that memory, network and GUI devices are
  not built, and the Windows/macOS runs still owed).
- [ ] **Closing runs**: `scripts/test-phase.sh --affected` in plain, asan and tsan (quote the `ran X of Y` lines); `ctest -L docs`; the Clang and GCC
  builds in unity and `-DENGINE_UNITY_BUILD=OFF`.
- [ ] **Series review**: `/code-review high` over the series' diff; findings recorded below.

**Done:** tests no longer need the global write sink to read log lines; the guide explains the devices; the closing runs are recorded.

## Post-implementation code review

(to be written)
