# Log everything / Day 5: remove `os::Print*`, the gate, the closing runs and the review

**Series:** [log everything](../log-everything.md)
**Depends on:** Days 1 to 4.

## Tasks

- [ ] Remove `Print`, `PrintError`, `PrintText`, `PrintErrorText`, `PrintLine`, `PrintBytes`, `PrintErrorBytes` from `os/console.h` (the compiler is the gate:
  nothing can print around the log system); update its header comment and `docs/reference`, and drop `<cstdio>` from the allow list entry if nothing needs it.
- [ ] CLAUDE.md: the rule "output goes through `out::`" in the wrappers table (replace `os::Print` in the console row) and `docs/guide/foundation-logging-and-assert.md`
  ("Common mistakes": printing with `os::Print`).
- [ ] Closing runs: `scripts/test-phase.sh --affected` plain, asan and tsan (this series touches every program: also `--all` plain), `ctest -L docs`,
  `docgen samples`, Clang and GCC unity builds and Clang non-unity; quote the `ran X of Y` lines.
- [ ] `/code-review high` over the series; findings recorded below. `STATUS.md`, `KNOWN-ISSUES.md` (the Windows text-mode difference: the C library no longer
  turns `\n` into `\r\n` on output; the PC runs are the user's).

**Done:** `os::Print*` is gone, everything prints through `out::`, the runs and the review are recorded.

## Post-implementation code review

(to be written)
