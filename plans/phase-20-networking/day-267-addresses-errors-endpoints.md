# Phase 20 / Day 267 — Addresses, Endpoints & Network Errors

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Day 266; Phase 10 (`String`, `StringView`); Phase 16 (wrappers).

The value types every other day uses: no syscalls, so they are tested exhaustively on any machine.

## Tasks

- [x] **`net::Address`** IPv4 and IPv6 (and IPv4-mapped) as 16 bytes plus a family tag; parse (`"127.0.0.1"`, `"::1"`, `"fe80::1%eth0"` scope id, bracketed forms) and format to the canonical text (RFC 5952 for IPv6); classification (loopback, any, link-local, private, multicast); ordering and hash. Parsing is strict: no leading zeros that read as octal, no trailing bytes, every length bounded.
- [x] **`net::Endpoint`** (address + port; `"host:port"` and `"[v6]:port"` parse and format), `net::EndpointSet` for resolver results (bounded).
- [x] **`net::Status`** and `net::ErrorCode` with portable codes (`Refused`, `Reset`, `TimedOut`, `Unreachable`, `WouldBlock`, `AddressInUse`, `AddressNotAvailable`, `NotConnected`, `AlreadyConnected`, `MessageTooLarge`, `Closed`, `Cancelled`, `Invalid`, `LimitExceeded`, `Other`), `[[nodiscard]]`, with the OS error number kept as a detail; mapping functions for the errno / WSA tables live in the backends (Days 268, 278); **their table tests move to those days** (amended 2026-11-11: nothing to map here).
- [x] **Byte order helpers** only through the foundation `bytes` module (`ByteWriter` big-endian forms); no `htons`/`ntohs` outside the backends.
- [x] **Tests** (`tests/net/address_test.cpp`): a table of valid and invalid address strings (about 55 and 85; the task said 200 each, the generated sweeps below carry the volume), parse-format round trips, every classification, a fuzz of random byte strings through the parser (never crashes, never accepts a string that fails to round-trip), Windows-style and POSIX scope ids; phase 20 added to `tests/phases.txt`.
- [x] **New module `engine/net`** (CMake targets with asan/tsan flavors, `modules.txt`, `ARCHITECTURE.md`, `docs/reference/modules.txt`); guide `docs/guide/net.md` started (addresses, errors).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** addresses and endpoints parse, format and classify every case of the table, errors are values with portable codes, and the module exists with its tests in phase 20.

## Outcome (2026-11-11, Linux machine)

- Built: `engine/net` (layer 11; `status.h`, `address.h`, `endpoint.h`), `tests/net/address_test.cpp` (17 test cases, 142,688 assertions), guide `docs/guide/net.md`, reference `docs/reference/net.md`. `ErrorCode` here is the day file's name (the spec said `NetError`; the spec now says `ErrorCode`).
- Deviations: the tables hold about 55 valid and about 85 invalid address texts and 11 + 35 endpoint texts, not 200 each; the rest of the coverage is generated (all IPv4 octet patterns, 4,000 random IPv6 addresses with zero runs, 60,000 random strings and every one-character mutation of the valid table through the parser). Byte-order helpers: none needed (no network byte order is touched; the backends convert in Day 268). The errno/WSA mapping tables are Days 268 and 278 (they are the backends'); no table test here.
- Runs: plain `ran 2518 of 6615 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the two known X11 failures), asan `ran 1959 of 6615`, tsan `ran 1641 of 6615`; GCC non-unity build of `net` and `net_tests` clean, tests pass; docs 7 of 7, 103 samples compiled, `docgen lint` clean for `net` (95 of 95 documented; the baseline also moved for ecs and storage, whose counts were stale).
- Open: the Windows PC build (`KNOWN-ISSUES.md`).

## Post-implementation code review

`/code-review high`: 10 findings. Fixed: `EndpointSet::operator[]` documented an abort nothing checked (now `ENGINE_ASSERT`); a numeric scope id of 1,000,000,000 or more was parsed as a name (now any all-digit scope up to 4294967295 is an id; regression checks added for 4294967295, 1000000000 and a format-parse round trip of 4000000000); `Aborts: never` relabelled `Fails: never`; the CMake comment said layer 12 (11); the phase-20 lines of `tests/phases.txt` moved out of the middle of the phase 19/18 entries; the per-character assertions of the random IPv6 test collapsed into one per address; the unused `status_try.h` include dropped; the Parse/WithScopeName duplicate scope validation removed; the day file's ticks and the table sizes corrected above. Declined: an empty `Endpoint` formats as `:0` which the parser refuses (it is an empty value shown in logs; documented on `Format`, pinned by a test).
