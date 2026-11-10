# Phase 17 / Day 233 — Inflate & PNG Decoder (`engine/image`)

**Phase:** [Phase 17 — 3D Text, Large 2D Text & Colored Glyphs](../phase-17-3d-text-and-color-glyphs.md)
**Depends on:** Day 222 (module placement).

The first image code in the engine: a small, safe inflate and PNG decoder, enough for `CBDT` and `sbix`, reused by Phase 21.

## Tasks

- [ ] **Module `engine/image`** (CMake library, flavors, `modules.txt`, `ARCHITECTURE.md`, `docs/reference/modules.txt`), no dependency above `foundation`.
- [ ] **`Inflate(ByteSpan zlib_stream, Vector<byte>* out, usize max_output) -> Status`:** zlib header and Adler-32, stored, fixed-Huffman and dynamic-Huffman blocks, output capped by `max_output` (a bomb returns `too_large`, never allocates past the cap); all table builds validated (over-subscribed or incomplete code lengths are errors); no recursion; time linear in input plus output.
- [ ] **`DecodePng(ByteSpan, Image* out, const PngLimits&) -> Status`:** signature, chunk CRCs, `IHDR`, `PLTE`, `tRNS`, `IDAT` concatenation, `IEND`; color types 0, 2, 3, 4, 6 at bit depth 8 (and 1, 2, 4, 16 for gray/palette per spec, converted to 8 bits), all five filters, no interlace (Adam7 is refused with `unsupported`, counted), limits on width, height and total bytes (`PngLimits`, defaults documented); output RGBA8 straight alpha, with a helper to premultiply.
- [ ] **`EncodePngStored(const Image&) -> Vector<byte>`** for tests and tools only (stored deflate blocks, CRC and Adler computed): the test-font builder of Day 231 uses it.
- [ ] **Tests** (`tests/image/inflate_test.cpp`, `png_test.cpp`): inflate against known zlib vectors for each block type (committed as byte arrays, produced by a script with Python's `zlib`, command recorded in the file), the maximum-output cap, truncated and corrupted streams (every prefix of a valid stream and a byte-flip sweep with a fixed seed: never a crash, never a hang, always a `Status`); PNG: a 1x1 of each color type and depth, each filter type, a palette with `tRNS`, a 64x64 gradient round trip through `EncodePngStored`, a CRC mismatch, an oversize header (`too_large`), Adam7 refused; a determinism test (same input, same output); a libFuzzer-style loop of 100,000 mutated inputs in the `[slow]` label.
- [ ] **Security notes** in the header: all sizes computed in 64-bit with overflow checks; no uninitialized output; decoding is allocation-bounded by the limits.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors asan on the full image tests (and the mutation loop under asan once); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 237; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/image.md` (new), `docs/reference/` regenerated.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the decoder reads the PNGs the color fonts use, refuses what it cannot read with a status, and survives every malformed input the sweeps produce.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
