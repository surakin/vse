# Phase 22 / Day 314 — Resource Ids, the Content Hash & the Versioned Blob Container

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Day 313; Phase 16 (wrappers); Phase 19 Day 256 (`ByteWriter`/`ByteReader`).

The value types and the container every later day writes into and reads from; pure CPU, tested on any machine.

## Tasks

- [ ] **`asset::ResourceType`** (a 32-bit tag as four characters, e.g. `MESH`, `TEXR`, `MATL`, `LEVL`) and a registry of known types with their current version.
- [ ] **`asset::ResourceId`** (64-bit hash of a normalized virtual path); path normalization (`/` separators, no `.` or `..` segments, no trailing slash, case-sensitive, UTF-8 NFC through the foundation string module, a length limit); `ResourceId::FromPath`, formatting for logs, a collision check used by the packer.
- [ ] **`asset::ContentHash`** (the hash chosen on Day 313) over bytes and over a stream of chunks; its published test vectors.
- [ ] **The blob header and sections:** magic, type, version, flags, section count, payload size, content hash, a section table (id, offset, size); `BlobWriter` (sections appended with alignment, hash filled at the end) and `BlobReader` (validates the header, every offset and size against the buffer before any use, rejects overlapping and out-of-range sections, verifies the hash on request).
- [ ] **Upgrade chain:** `asset::UpgradeRegistry` maps (type, from version) to a function that rewrites a blob one version up; `UpgradeToCurrent` applies them in order and fails on a gap; a type without upgrades fails for an old version with a clear error.
- [ ] **Errors are values** (`asset::Status`, `asset::ErrorCode`: `NotFound`, `Corrupt`, `UnsupportedVersion`, `TypeMismatch`, `TooLarge`, `Io`, `Cancelled`, `Offline`, `LimitExceeded`).
- [ ] **Tests** (`tests/asset/blob_test.cpp`, `id_test.cpp`): hash vectors; id and path-normalization tables (valid and invalid); write-read round trips of blobs with 0 to 16 sections; every truncation and every single-byte corruption of a golden blob is rejected or caught by the hash, never crashes; a golden old-version blob upgrades to the current version; a fuzz of random bytes against `BlobReader` (a million cases, seeded).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 330; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** ids, hashes and blobs round-trip, every malformed blob is refused, and the upgrade chain is tested with a golden old blob.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
