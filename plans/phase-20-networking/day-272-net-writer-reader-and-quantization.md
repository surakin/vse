# Phase 20 / Day 272 — Message Encoding: NetWriter / NetReader, Bit Packing & Quantized Values

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Day 267; Phase 19 Day 256 (`foundation/bytes/ByteWriter`, `ByteReader`).

The wire vocabulary for messages, extending the bounds-checked foundation primitives rather than re-implementing them.

## Tasks

- [ ] **`net::NetWriter` / `NetReader`** over `Span`: bit-level writes (`WriteBits(value, n)`, bools, ranged integers `[min,max]` using the fewest bits), byte-aligned fast paths delegating to `ByteWriter`/`ByteReader`, variable-width integers (the Phase 19 canonical varint), strings and byte arrays with a mandatory maximum length, align-to-byte; **every read can fail with an error value and leaves the reader usable and its position unchanged**; no pointer casts.
- [ ] **Quantized values:** floats in `[min,max]` with `n` bits (round-to-nearest, error bound documented and tested), `Vec2/3/4` and unit quaternions ("smallest three") with documented error bounds, angles; the quantization is deterministic across compilers and platforms (no fused multiply-add dependence: tested with the Linux compilers now and the Windows PC on Day 278).
- [ ] **Delta helpers** `WriteDelta`/`ReadDelta` for integers and quantized values against a baseline (no replication logic; the hook the spec records).
- [ ] **Tests** (`tests/net/net_codec_test.cpp`): round trips of 1 million random mixed sequences; every truncation point fails cleanly; overlong varints, oversized strings and out-of-range values rejected; quantization error within the bound over a dense sweep including boundaries, NaN (rejected or mapped per documented rule), infinities and -0; golden byte vectors checked in so a format change is a visible diff.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a message can be written and read with bit packing and quantized values, every malformed input is an error value, and the golden bytes pin the format.
