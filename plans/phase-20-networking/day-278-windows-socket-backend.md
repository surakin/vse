# Phase 20 / Day 278 — Windows Socket Backend (Windows PC)

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Days 268-277; Phase 13 (the Windows platform layer).

The Windows implementation of `BACKEND.md`, done on the Windows PC, proving the interface fits a second backend. Planned as the day the Windows PC builds and tests all of Days 267-277.

## Tasks

- [ ] **Winsock backend** in `engine/net/windows/` (`WSAStartup` once through the platform layer, `WSA*` calls only there; `winsock2.h` only through `engine/net/windows/winsock.h` as `windows.h` goes through `win32.h`): non-blocking sockets (`ioctlsocket`), error mapping from `WSAGetLastError` through the Day 267 table, `SO_EXCLUSIVEADDRUSE` instead of `SO_REUSEADDR` semantics, dual stack, `WSAECONNRESET` on UDP handled (ignored via `SIO_UDP_CONNRESET`).
- [ ] **Readiness:** the Day 266 decision (IOCP via `AcceptEx`/`WSARecv` overlapped, or `WSAPoll` first) implemented behind the same `Reactor` interface and awaitables; the same race rules and tests as Day 269.
- [ ] **Resolver:** `getaddrinfo` on the resolver thread; numeric hosts without the OS.
- [ ] **Gate and docs:** `net_access_check` Windows entries; `BACKEND.md` Windows notes; `docs/guide/windows.md` and `BUILD.md` updated if a prerequisite changes (none expected: `ws2_32` ships with the OS).
- [ ] **Build and test on the Windows PC:** `windows-msvc` (plain, asan) and `windows-clang-cl`, both unity modes; the whole of `tests/net` including the conformance suite on TCP and UDP and the codec golden bytes (the quantization determinism check of Day 272); fix what breaks or list it in `KNOWN-ISSUES.md`.
- [ ] **Records:** every Day 267-277 entry "open until the Windows PC runs it" is cleared or restated with the failure.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the Windows backend passes the same `tests/net` as Linux on both Windows compilers, or the differences are listed with their reasons.

## Notes

- This day is done on the Windows PC; if no Windows machine is available when the phase reaches it, it stays open and Day 281 records it as open (as Phase 19 did).
