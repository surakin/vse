# Phase 22 / Day 316 — The Pak Format: Writer, Reader & Pak Backend

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 314-315.

The shipping container: many resources in one file, found by id.

## Tasks

- [ ] **Format** (as the spec settled it): header (magic, version, alignment, **the target platform it was packed for**, Day 327), entry data aligned to 64 bytes (and to the page size above 1 MiB) so a ranged read lands in an aligned destination buffer with no copy afterwards, the index (sorted by `ResourceId`: id, offset, size, content hash, type), a footer (index offset, entry count, index hash); no compression in version 1 (a flag reserved).
- [ ] **`PakWriter`:** adds (id, bytes), refuses duplicate ids and id collisions between different paths (the id <-> path table is written into the pak as a `NAMES` blob section, read only when `ReadTable` asks, never on the open or load path; a shipping pak may leave it out with a flag, and the pak then reports ids only), writes through `io::File`, finishes with the index and footer; deterministic (entries in id order, padding zero-filled, no time stamps).
- [ ] **`PakReader`:** opens, validates the footer, checks the index against the file size before using it (counts and offsets bounded; entries in range, sorted, no duplicates), builds a hash table over the sorted index at open (Day 313 Spike 1: 0.42 ms and 5 ns a lookup for 100,000 entries, against 79 ns for a binary search; nothing extra is stored in the file) and looks up by id, exposes ranged reads; never trusts an offset.
- [ ] **Versions announced and enforced:** the pak header carries its own format version and the current version of each resource type it holds (a small table); the reader refuses an older pak (`UnsupportedVersion`), and the writer refuses a blob that is not the current version of its type or whose hash does not verify (the packer is an enforcement point).
- [ ] **Targets enforced (2026-11-13):** `PakWriter` takes a target tag; `PakReader` and `PakBackend` refuse a pak packed for another target with `WrongTarget` naming both and the fix (`asset_mint pack --target`); a blob's own header carries the target it was laid out for, so a loose blob for another target is refused the same way; a test per refusal.
- [ ] **`PakBackend`** implementing `ResourceBackend` (read-only, `Watch` reports nothing) and joining the conformance suite.
- [ ] **Closure is complete or declared:** `PakWriter::Finish` verifies that every dependency of every blob is in the pak, with the declared type and version, or in a base pak that this pak names by content hash in its header (patch paks); an incomplete closure is a pack error listing each missing (blob, dependency) pair; `PakBackend::Open` refuses a pak whose named base pak is not mounted with that content hash (`MissingDependency`); tests: a pak missing one texture, a pak with a wrong-type dependency, a patch pak with and without its base.
- [ ] **`asset_mint pack`** first cut (the tool arrives fully on Day 327): `tools/asset_mint` skeleton with a `pack` command that packs a directory of blobs; tests drive the library, not the tool.
- [ ] **Tests:** a pak of 100,000 entries (open under the spike's bound), every footer or index corruption and every truncation refused, two packs of the same inputs identical byte for byte, lookups of absent ids, a 4 GiB offset test through a sparse test double (offsets are 64-bit).
- [ ] **Benchmark** (hidden `[.][asset-bench]`): open time, lookup, and a 1 MiB ranged read, recorded for Day 329.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 330; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a pak writes deterministically, opens and serves ranged reads through the backend interface, passes the conformance suite and refuses every corrupted file.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
