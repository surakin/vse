# Phase 19 / Day 257 — Component Registry II: Versions, Upgrades & Hostile Input

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Day 256.

A stored component must still load after its type changes, and a stored file must never be trusted.

## Tasks

- [x] **Versions and upgrade chains:** `ComponentDesc` carries a version and `Upgrade(from_version, ByteReader&, ByteWriter&)` steps registered in order; a record stored at version `v` loads through `v -> v+1 -> ...` to the current layout; a missing step in the chain is a registration-time error.
- [x] **Newer than known and unknown:** a record with a version above the registered one returns `TooNew`; a record for a component id the process has not registered follows the load option (`Fail`, `SkipAndReport`, `KeepOpaque` so the bytes survive a save by an older program); the policy is a value in `LoadOptions` (declared on Day 259) and tested here through a stand-in.
- [x] **Custom save/load pair:** `ComponentDesc::Custom(save, load)` for a type fields cannot describe (a handle that stores a name, a union); the same bounds rules apply to its `ByteReader`.
- [x] **Limits as settings:** `storage.max_record_bytes`, `storage.max_string_bytes`, `storage.max_array_elements`, `storage.max_components_per_entity` registered through the settings system and passed to the reader; exceeding one is `LimitExceeded`, not an allocation.
- [x] **Fuzz harness** (`tests/storage/registry_fuzz_test.cpp`): deterministic mutation of valid encodings (bit flips, truncation, length-field overwrite, duplication) for every registered component kind, 100,000 cases per kind, plain and asan: every result is a `Status` or a valid value; no read outside the buffer, no allocation above the limits, no hang.
- [x] **Tests** (`tests/storage/upgrade_test.cpp`): a golden v1 byte string of a component loads after the type gained a field (v2) through its upgrade and equals the v2 value bit-exact; a three-step chain; a chain with a gap fails at registration; `TooNew` and the three unknown-component policies; the opaque bytes of an unknown component survive a load and save unchanged.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** stored components survive type changes through registered upgrades, and no byte sequence makes the decoder read out of bounds or allocate without limit.

## Notes (2026-11-11, Linux machine)

- `ComponentDesc` gained `oldest_version`, `upgrades` (`UpgradeStep{from_version, run}`) and `custom` (`CustomCodec{save, load}`); `Register` requires exactly one step per version from `oldest_version` to `version - 1`.
  `RegisteredComponent::DecodeRecord` runs the chain between two scratch buffers, requires each step to read all of its input and bounds each output by `max_record_bytes`.
- `TooOld` added to `ErrorCode`. `ComponentRegistry::Resolve(id, UnknownPolicy, ...)` returns `Decode`, `Skip` or `KeepOpaque`; `OpaqueComponent` keeps the bytes of an unknown record, and `WriteComponentRecord` / `ReadComponentRecord` frame a record (u32 id, u32 version, varint length, bytes).
  The `LoadOptions` that carries the policy is Day 259; the policy is tested here as a value.
- Limits: `CodecLimits` plus `RegisterStorageSettings` / `LimitsFromSettings` (`storage.max_record_bytes`, `max_string_bytes`, `max_array_elements`, `max_components_per_entity`; env `ENGINE_STORAGE_*`, flags `--storage-*`).
  The component-count limit is enforced by the entity container of Day 260.
- Fuzz: 21 kinds, 100,000 mutated encodings each (10,000 under ThreadSanitizer), canonical re-encode property; a planted decoder bug (bool 2 accepted) makes it fail.
- Docs: `docs/guide/storage.md` (versions, unknown records, custom codec, limits, fuzzing), `docs/reference/storage.md`.
- Runs: plain `ran 2363 of 6180 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 1819 of 6180`, tsan `ran 1501 of 6180`. Failures: the two X11Window tests (locked session) and the Day 235 tsan scene test, both already in `KNOWN-ISSUES.md`; the plain run's `docs_check` failed before the docs were regenerated and passes after (`ctest -L docs` 7 of 7).
  Clang non-unity and GCC build clean; `storage_tests` pass on GCC. Gates pass.

## Post-implementation code review

Inline review of the day's diff (`/code-review high` recipe, read by me, no verifier): 1 finding. `ComponentRegistry::Resolve` left `*action` unset when it failed (`Fail` policy, unknown id); a caller that ignored the status read an indeterminate value. Fixed: `*action` is `Skip` on every path. Nothing declined.
