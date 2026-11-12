# Phase 20 / Day 272 — Message Encoding: NetWriter / NetReader, Bit Packing & Quantized Values

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Day 267; Phase 19 Day 256 (`foundation/bytes/ByteWriter`, `ByteReader`).

The wire vocabulary for messages, extending the bounds-checked foundation primitives rather than re-implementing them.

## Tasks

- [x] **`net::NetWriter` / `NetReader`** over `Span`: bit-level writes (`WriteBits(value, n)`, bools, ranged integers `[min,max]` using the fewest bits), byte-aligned fast paths delegating to `ByteWriter`/`ByteReader`, variable-width integers (the Phase 19 canonical varint), strings and byte arrays with a mandatory maximum length, align-to-byte; **every read can fail with an error value and leaves the reader usable and its position unchanged**; no pointer casts.
- [x] **Quantized values:** floats in `[min,max]` with `n` bits (round-to-nearest, error bound documented and tested), `Vec2/3/4` and unit quaternions ("smallest three") with documented error bounds, angles; the quantization is deterministic across compilers and platforms (no fused multiply-add dependence: tested with the Linux compilers now and the Windows PC on Day 278).
- [x] **Delta helpers** `WriteDelta`/`ReadDelta` for integers and quantized values against a baseline (no replication logic; the hook the spec records).
- [x] **Tests** (`tests/net/net_codec_test.cpp`): round trips of 1 million random mixed sequences; every truncation point fails cleanly; overlong varints, oversized strings and out-of-range values rejected; quantization error within the bound over a dense sweep including boundaries, NaN (rejected or mapped per documented rule), infinities and -0; golden byte vectors checked in so a format change is a visible diff.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a message can be written and read with bit packing and quantized values, every malformed input is an error value, and the golden bytes pin the format.

## Outcome

New: `engine/net/codec.h` (`NetWriter` over a caller's buffer, `NetReader`, `CodecError`, `Quantize`/`Dequantize`): bit packing from the low bit (byte-aligned integers equal the little-endian `ByteWriter` bytes), ranged integers, canonical varints at any bit offset, zigzag varints, length-limited strings and byte arrays (length checked before allocation), `AlignToByte` (reader insists on zero padding), quantized floats (clamp out of range and infinities, `-0` is 0, NaN rejected), Vec2/3/4, smallest-three quaternions, angles, integer and quantized deltas. Failed reads and writes change nothing. Quantization runs in `f64` with each product stored in a volatile before reuse, so no fused multiply-add can differ between compilers. Documentation: `docs/guide/net.md` ("Messages: the bit-packed codec", one compiled sample), `docs/reference/net.md`; docgen lint at zero, `ctest -L docs` passes.

Tests (`tests/net/net_codec_test.cpp`): 1,000,000 random mixed sequences (60,000 under a sanitizer), every truncation point, golden bytes worked out by hand, dense quantization sweeps over ten widths and four ranges against the documented bound, fixed rounding-edge levels, NaN/infinity/-0, rotations (20,000 per width) and angles within bound, deltas including the ends of i64, hostile varints/lengths/padding. Compilers: Clang unity, GCC, Clang non-unity and an optimized tree all pass the codec tests (the same golden bytes and levels). The Windows PC checks it on Day 278.

Runs (Linux machine): plain `ran 2646 of 6970 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 2068 of 6970 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`, tsan `ran 1744 of 6970 tests (phases: all; flavors: tsan; display: included; crash tests: excluded)` (only the known tsan scene test failed).

## Post-implementation code review

`/code-review high`: 4 findings, 2 fixed, 2 declined.

- Fixed: `ReadDeltaQuantized` added the baseline level to a hostile difference before checking it (signed overflow near the ends of i64); the difference is checked first, with a test for the extremes (no sanitizer in the flavors catches signed overflow, so the test pins the result, not the old crash). `WriteString`/`ReadString` used `reinterpret_cast`; they now go byte by byte.
- Declined: an allocation failure in `ReadBytes`/`ReadString` is reported as `TooLong` (the length is already bounded by the caller's maximum; a distinct code is not worth a new enum value); the writer zeroes its whole buffer (documented; it keeps padding bits zero without extra work).
