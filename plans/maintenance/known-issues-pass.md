# Maintenance: small known-issue fixes and the settings leftovers

Not a numbered phase. Origin: the user's pass over `KNOWN-ISSUES.md` (2026-11-07). Each task removes (or rewrites) its entry in the commit that fixes it.

## Tasks

- [x] `-Wsubobject-linkage` warnings from `engine/rhi/null/null_device.cpp` in unity builds: give the types external-linkage-safe names or move them (CLAUDE.md unity rules).
- [x] `rhi::detail::GrowArray` (`engine/rhi/grow_array.h`, 12 uses) replaced by `Vector`; header removed.
- [x] `raw_new_check`: a `new` separated from its type across lines is found; `tools/` is scanned (with its allow list if needed); planted self-tests.
- [x] A TSan flavor of the foundation tests covering `SharedPtr` with `AtomicRefCount` (a test that runs under TSan).
- [x] Arena allocator tests that fail on the two planted faults recorded on Phase 6 Day 90.
- [x] `u128`/`i128` get `operator<=>`: the cross-syntax shim gets `<compare>`.
- [x] `tests/io` removes its temporary directory when a run is interrupted, and sweeps stale `/tmp/re_io_test.*` of earlier runs.
- [x] `CreatePrivateDirectory` checks that existing parents are owned by the user (or root) and not group/world writable; test.
- [x] Crash dumps in `<dump directory>/saved/` are pruned (keep the newest N); test.
- [x] Settings: `EnvironmentSnapshot::Capture` moves behind an `os::` call (`os::EnvironmentVariables`), Linux implementation, Windows/macOS checklist line.
- [x] Settings: a change updates the snapshot incrementally instead of rebuilding every slot; crash state formats only changed settings; test that a snapshot taken before a change is unchanged.
- [x] Settings: `--list-settings` names the command-line flag as the source of a legacy-flag value even when a file or the environment also sets the key.
- [x] `KNOWN-ISSUES.md` entries removed or rewritten; `STATUS.md`; docs regenerated (`docgen lint` zero, `ctest -L docs`); scoped tests (plain/asan/tsan); `/code-review high` recorded below.

**Done:** every box checked, the entries gone, tests and docs green, the review recorded.

## Result

All items done; each entry removed from `KNOWN-ISSUES.md` (the settings entry now lists only what remains). Scoped runs: plain `ran 1341 of 3464 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 965 of 3464`, tsan `ran 682 of 3464`; `ctest -L docs` passes.
Notes: planted-fault checks for the arena tests were run by hand (both Phase 6 Day 90 mutations fail the new tests). Not tested: the stale-directory sweep's age rule (no API sets a modification time) and the negative case of the ancestor-ownership check (needs a directory owned by another user). Behavior change worth knowing: the demos' old flags (`--no-vsync`, `--validation`) now live in the command-line layer, so they are no longer written to the user's settings file.

## Post-implementation code review

`/code-review high`: 10 findings. Fixed: `memory_only` is a parameter of `DoApply`, not state that a rejected call could leave set; `SetFromCommandLine` keys the layer by the canonical name and records the value only after it was accepted (test added); the ancestor check rejects only an ancestor another user owns and others can write to without the sticky bit; a dump whose time cannot be read is skipped, not treated as oldest; `EnvironmentEntry` walks only to its index and `Capture` stops at the end; the temp-dir sweep guard is atomic; `raw_new_check` exempts only `tools/*/fixtures/`; two redundant remove loops became `Clear()`. Declined: the named `null_impl` namespace with a using-directive in `null_device.cpp` (the name is unique to that file, it builds in unity and plain modes, and a collision would be a compile error, not silent).
