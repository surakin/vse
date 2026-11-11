# Phase 20 / Day 267 — Addresses, Endpoints & Network Errors

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Day 266; Phase 10 (`String`, `StringView`); Phase 16 (wrappers).

The value types every other day uses: no syscalls, so they are tested exhaustively on any machine.

## Tasks

- [ ] **`net::Address`** IPv4 and IPv6 (and IPv4-mapped) as 16 bytes plus a family tag; parse (`"127.0.0.1"`, `"::1"`, `"fe80::1%eth0"` scope id, bracketed forms) and format to the canonical text (RFC 5952 for IPv6); classification (loopback, any, link-local, private, multicast); ordering and hash. Parsing is strict: no leading zeros that read as octal, no trailing bytes, every length bounded.
- [ ] **`net::Endpoint`** (address + port; `"host:port"` and `"[v6]:port"` parse and format), `net::EndpointSet` for resolver results (bounded).
- [ ] **`net::Status`** and `net::ErrorCode` with portable codes (`Refused`, `Reset`, `TimedOut`, `Unreachable`, `WouldBlock`, `AddressInUse`, `AddressNotAvailable`, `NotConnected`, `AlreadyConnected`, `MessageTooLarge`, `Closed`, `Cancelled`, `Invalid`, `LimitExceeded`, `Other`), `[[nodiscard]]`, with the OS error number kept as a detail; mapping functions for the errno / WSA tables live in the backends (Days 268, 278) but their tests run from a table here.
- [ ] **Byte order helpers** only through the foundation `bytes` module (`ByteWriter` big-endian forms); no `htons`/`ntohs` outside the backends.
- [ ] **Tests** (`tests/net/address_test.cpp`): a table of 200 valid and 200 invalid address strings, parse-format round trips, every classification, a fuzz of random byte strings through the parser (never crashes, never accepts a string that fails to round-trip), Windows-style and POSIX scope ids; phase 20 added to `tests/phases.txt`.
- [ ] **New module `engine/net`** (CMake targets with asan/tsan flavors, `modules.txt`, `ARCHITECTURE.md`, `docs/reference/modules.txt`); guide `docs/guide/net.md` started (addresses, errors).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** addresses and endpoints parse, format and classify every case of the table, errors are values with portable codes, and the module exists with its tests in phase 20.
