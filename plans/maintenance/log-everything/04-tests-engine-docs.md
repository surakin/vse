# Log everything / Day 4: tests, `engine/audio` and the documentation samples

**Series:** [log everything](../log-everything.md)
**Depends on:** Day 1.

## Tasks

- [ ] The rename in the 52 files of `tests/` (113 calls) and the one in `engine/audio/audio_system.cpp` (as a `Log` line if it is a diagnostic).
- [ ] Tests that read what a program printed (child-process tests, CLI goldens) are unchanged in what they compare; `tests/support` helpers that call the old
  functions are renamed.
- [ ] The documentation samples (`docs/guide/*.md`, 21 files, 134 calls) and `docs/samples-prelude.h`: `os::Print` becomes `out::Print` with the include; the
  `output` blocks stay as they are. `docgen samples` compiles and runs them on Day 5.
- [ ] Everything builds clean (Clang unity).

**Done:** no `os::Print*` call is left outside `os/console.h`.
