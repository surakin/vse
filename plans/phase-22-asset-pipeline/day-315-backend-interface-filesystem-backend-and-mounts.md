# Phase 22 / Day 315 — The Resource Backend Interface, the Filesystem Backend & Mounts

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Day 314; Phase 9 (`io::File`, `io::Path`, `io::Directory`).

The seam every backend implements, and the first backend: loose files for development.

## Tasks

- [ ] **`asset::ResourceBackend`** interface: `Stat(id, &info)` (size, content hash if known, modification stamp), `Open(id, &reader)` returning a `ResourceReader` (`Size`, `ReadAt(offset, bytes)` synchronous, `ReadAtAsync` for backends that can), `List(prefix, &ids)` (bounded), `Watch(&changes)` (polling, returns ids changed since the last call), `Name()`; `Capabilities` bits (ranged, async, watch, writable=false).
- [ ] **`FilesystemBackend`** over `io::`: a root directory; virtual path to file path mapping that refuses escapes (`..`, absolute paths, symlink loops by a depth limit); the id-to-path index built by walking the directory once and refreshed by `Watch` (modification stamps); reads through `io::File` ranges.
- [ ] **`LoadBufferPool`** (Day 313 Spike 5: a read into a fresh buffer is 4x slower than into a pre-faulted one, 75 ms against 17.5 ms per 256 MiB): a pool of 64-byte-aligned buffers (page-aligned above 1 MiB) whose pages are touched when the buffer is created, handed out with a size class, returned on release, bounded by `assets.buffer_pool_bytes`; `ResourceReader::ReadAt` reads into a pool buffer; a test shows a second load of the same size reuses a buffer and allocates nothing.
- [ ] **`FilesystemBackend` and ids:** the id index is built from the `.id` sidecars and the minted `NAMES` blob, never from file names; two files claiming one id are refused naming both; a copied file with its sidecar (a duplicated id) is refused; a source with no sidecar is minted only by the tool that writes one.
- [ ] **`MountTable`:** ordered mounts (`/` prefix to backend, priority); lookup returns the highest-priority backend that has the id; a loose-file mount over a pak mount overrides it; unmount; a snapshot for tests.
- [ ] **A test double backend** (`MemoryBackend`, with injectable failures and latency) that every later day's tests use, like the file layer's network-like double.
- [ ] **Backend conformance suite** (`tests/asset/backend_conformance.h`): written once against `ResourceBackend`; run against the filesystem backend and the memory backend (the pak and network backends join on Days 316 and 326).
- [ ] **Tests:** path escape attempts, a file that disappears between `Stat` and `Open`, a directory with 10,000 entries, a change seen by `Watch`, mount priority and override, a read past the end.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 330; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the filesystem backend serves a directory through the interface, the conformance suite passes on it and on the memory backend, and mounts override by priority.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
