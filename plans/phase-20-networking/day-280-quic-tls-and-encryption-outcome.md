# Phase 20 / Day 280 — The QUIC / TLS / Encryption Outcome (the User's Decision)

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Days 266 (the decision), 273 (the conformance suite), 277, 279.

Implements whichever option the user chose after Day 266's evidence (`docs/superpowers/specs/2026-11-11-networking-design.md`, section 3 (3); the recommendation is branch (d) with QUIC and TLS deferred, branch (e); **the user has not decided yet**). **This day is conditional: it is rewritten in the commit that records the decision, before it starts**, because the options differ completely; the shared tasks below hold for all of them.

## Tasks

- [ ] **Decision recorded** in the spec and `PLAN.md` assumption #26 (what, why, license, size, per-platform ports), and `BUILD.md` if a prerequisite is added; the day's task list below is replaced by the chosen branch before any code.
- [ ] **Branch (a) vendored QUIC library:** vendored under `third_party/<name>/` (pinned version and hash, built from source, only its facade includes its headers, a boundary gate like `sqlite_boundary_check`); a `QuicTransport` behind the Day 273 interface (streams map to reliable channels, datagrams to unreliable ones); its UDP I/O goes through the Day 269 reactor if the library allows, else through its callbacks with the thread rules documented; certificate handling and trust store decisions recorded.
- [ ] **Branch (b) operating-system QUIC:** only on the platforms that have it; the interface and conformance suite run there; the other platforms report `Unsupported`.
- [ ] **Branch (c) TLS on the TCP transport:** a vendored or OS TLS library behind a facade used only by `TcpTransport`; certificate and trust handling; the conformance suite runs over TLS.
- [ ] **Branch (d) authenticated encryption over the UDP transport:** an audited primitive from a vendored library (never custom cryptography); key exchange and the handshake cookie replaced by the primitive's constructs; replay protection; conformance suite over the encrypted transport.
- [ ] **Branch (e) none now:** the guide and spec say "trusted networks only", the handshake cookie remains documented as anti-spoofing only, and QUIC is a recorded deferral with its trigger; the day shrinks to documentation and the tests of the threat notes.
- [ ] **Whichever branch:** the conformance suite, the security tests of Day 279 and a certificate/key-handling test pass on it; `module_deps_check`, `std_usage_check` and the boundary gate cover the new code; third-party warnings stay out of our `-Werror` targets as for other vendored code.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the chosen option is implemented behind the Transport interface with the conformance and security suites passing, or the deferral is recorded with its reason and trigger.

## Notes

- Needs the user's answer to Day 266's question; nothing in Days 267-279 depends on it.
