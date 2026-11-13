# Phase 22 / Day 325 — The Resource Server & the Network Protocol

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 314-316 and 323; Phase 20 (`net::TcpTransport`, `net::RateLimiter`).

The serving half of the network backend: a small protocol over the TCP transport and a tool that speaks it.

## Tasks

- [ ] **Protocol v1** (request and response messages on `net::Transport`, encoded with `NetWriter`/`NetReader`): `Hello` (version, limits), `Stat(id)`, `Table(page)` (the server's id <-> path table, paged, with a generation number so a client fetches only a changed table) and `List(prefix, page)`, `Read(id, offset, length)` with a response of at most `assets.net.max_read_bytes`, errors as codes (`NotFound`, `Range`, `TooLarge`, `Busy`); every length bounded before allocation; a request id so responses can interleave.
- [ ] **`ResourceServer` library class** (in `engine/asset`, usable in a game for a LAN host): serves any `ResourceBackend` (a directory or a pak) to many connections; per-connection in-flight limit; the transport's rate limits and budgets are used as they are; read-only (there is no write request in the protocol).
- [ ] **`tools/resource_server`:** a command line (`--root DIR | --pak FILE`, `--listen ADDR:PORT`) over `SocketStreamProvider`; its options are its own command line like `net_probe`'s (documented exception in `config_access_check`).
- [ ] **Tests:** a server and a raw protocol client over `SimStreamNetwork`: stat, list in pages, ranged reads (including the last partial range and a read past the end), oversized requests refused, malformed requests close the connection, 200 clients at once, the server survives a client that sends garbage (a fuzz of random and mutated request bytes against a live server: counts stay within caps).
- [ ] **Docs:** the protocol written in `docs/guide/assets.md` (message table, limits, errors) and the spec.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 330; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a resource server serves a directory or a pak over the TCP transport with bounded requests and responses, and survives malformed and abusive clients.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
