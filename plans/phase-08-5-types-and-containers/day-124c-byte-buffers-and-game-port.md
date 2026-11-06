# Phase 8.5 / Day 124c — Byte Buffers and the `game/` Port

**Phase:** [Phase 8.5 — Standard Types and Container Hygiene](../phase-08-5-types-and-containers.md)
**Depends on:** Day 124b.

Use the vocabulary in the code the project owns. Files touched: the `unsigned char`/`uint8_t` byte buffers of `engine/` (about 55 sites), `game/common/textures.*`, `game/rhi_demo`, `game/rhi_bench`, `game/renderer_demo`, and the file layer's `(const void*, usize)` pairs if the spec adds `ByteSpan` overloads.

## Tasks

- [ ] Replace byte-buffer spellings in `engine/` (`foundation`, `jobs`, `rhi`, `renderer`) with `byte`/`u8`, site by site from the Day 124a inventory (the file layer's `Read`/`Write`, `Vector<unsigned char>` in the shader reflection and the vertex data, the hash and text helpers); keep and annotate the documented API-boundary uses (Vulkan structures, `char` for text, libc
      signatures). No behavior change: the full suite is the check.
- [ ] The file layer's byte interfaces: `File::Read`/`Write`/`Append`, `ReadAll`, `WriteFile`, `WriteFileAtomic` get `ByteSpan`/`MutableByteSpan` overloads (the `(const void*, usize)` forms stay as thin wrappers where many callers use them; the spec's decision), and `Vector<byte>` is what `ReadAll` fills.
- [ ] `game/common/textures.*`: `Pixels::rgba` and `RtexImage::data` become `Vector<byte>` (the engine container; the demos already use it for the renderer's pixels), `WritePpm`/`WriteRtex`/`ReadRtex` use it, the `std::vector` buffers in the writers go away; callers in `rhi_demo`, `renderer_demo`, `rhi_bench` and `tests/rhi` follow.
- [ ] `game/rhi_demo` (`std::unique_ptr<UniformRing>` becomes `UniquePtr`, `std::vector<unsigned char>` pixel buffers become `Vector<byte>`, `std::string` paths become `io::Path`), `game/rhi_bench` and `game/renderer_demo`/`renderer_bench`: no `std::` container, string or smart pointer left.
- [ ] Anything the ports reveal as missing from the engine's containers (a `Vector::Assign(range)`, `Append(ByteSpan)`, `String::Reserve`, a `HashSet`, ...) is added with tests in this day, not worked around.
- [ ] Tests: everything that passed before passes (the demo ctests, golden images byte for byte, `rtex` round trips); new cases for each container feature added; the Phase 8 file-layer tests with the span overloads.
- [ ] **Documentation:** labels on the new overloads and container features; the guide pages (`io.md`, `foundation-containers.md`) updated; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` (foundation headers change: the whole suite), the asan and tsan flavors, and both demos 1000 frames with `--validation` on Wayland and X11; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`.
- [ ] Update `STATUS.md`, the spec's inventory table (mark the rows done); commit.

**Done:** the engine and the game code spell bytes `byte`, `game/` contains no banned `std::` use, the file layer takes spans, and the full suite and both demos behave exactly as before.
