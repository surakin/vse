# Phase 22 / Day 331 — The Network Backend: Cache, Retry & Offline Mode

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Day 330; Phase 20 (`net::TcpTransport`, `Clock`).

The client half: a `ResourceBackend` whose resources live on a server, with a content-hash cache on disk.

## Tasks

- [ ] **`NetworkBackend`** implementing `ResourceBackend`: connects through a `net::Transport` it is given (so tests use the simulated stream and the loopback), `Stat`/`List`/ranged `Open` over the protocol, **`ReadTableAsync` natively over the paged `Table` request (no thread parked on the connection; pages handed out as they arrive; the generation number makes a refresh fetch only a changed table, and an offline backend serves the cached table with `Offline` marked)**, one request in flight per reader and many per connection, timeouts from settings.
- [ ] **The disk cache:** files named by content hash in `assets.cache_dir` (written through `io::`, atomically replaced), an index of id to hash, a size limit with least-recently-used eviction, verification of a cached blob's hash when it is fetched and when the cache is opened (not on every read: the cache is local and the hash is stored with the index), so a corrupt entry is dropped and refetched without taxing the load path; verification of a cached blob's hash before use (a corrupt cache entry is dropped and refetched), and whole-blob prefetch for small resources while large ones are read by range.
- [ ] **Retry and back-off:** a failed or timed-out request retries with bounded exponential back-off and jitter (the jitter from a seeded generator in tests); after `assets.net.max_retries` the backend reports `Offline`.
- [ ] **Offline mode:** while offline, `Stat` and reads are served from the cache when the cache holds the id and its hash matches the last known one; misses return `Offline`; the backend reconnects in the background and reports `Online` through its state; the resource manager treats `Offline` like `NotFound` for fallbacks but does not cache the failure.
- [ ] **Mount integration:** the network backend mounts like any other (`/` over a pak for development with a remote content server); the conformance suite runs on it.
- [ ] **Tests:** the conformance suite over a sim stream; cache hit, miss, eviction and corruption; a server that disappears mid-read and returns (loss, latency and disconnect injected through the sim network); offline serving from the cache; a content change on the server seen by `Watch`; real loopback sockets with the `resource_server` library class (label `network`); asan and tsan for the background reconnect.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 338; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the network backend passes the conformance suite, caches by content hash, retries with back-off, serves from the cache offline, and recovers when the server returns.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
