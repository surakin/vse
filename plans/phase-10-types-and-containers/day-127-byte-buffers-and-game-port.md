# Phase 10 / Day 127 — Byte Buffers and the `game/` Port

**Phase:** [Phase 10 — Standard Types and Container Hygiene](../phase-10-types-and-containers.md)
**Depends on:** Day 126.

Use the vocabulary in the code the project owns. Files touched: the `unsigned char`/`uint8_t` byte buffers of `engine/` (about 55 sites), `game/common/textures.*`, `game/rhi_demo`, `game/rhi_bench`, `game/renderer_demo`, and the file layer's `(const void*, usize)` pairs if the spec adds `ByteSpan` overloads.

## Tasks

- [x] Replace byte-buffer spellings in `engine/` (`foundation`, `jobs`, `rhi`, `renderer`) with `byte`/`u8`, site by site from the Day 125 inventory (the file layer's `Read`/`Write`, `Vector<unsigned char>` in the shader reflection and the vertex data, the hash and text helpers); keep and annotate the documented API-boundary uses (Vulkan structures, `char` for text, libc
      signatures). No behavior change: the full suite is the check.
- [x] The file layer's byte interfaces: `File::Read`/`Write`/`Append`, `ReadAll`, `WriteFile`, `WriteFileAtomic` get `ByteSpan`/`MutableByteSpan` overloads (the `(const void*, usize)` forms stay as thin wrappers where many callers use them; the spec's decision), and `Vector<byte>` is what `ReadAll` fills.
- [x] `game/common/textures.*`: `Pixels::rgba` and `RtexImage::data` become `Vector<byte>` (the engine container; the demos already use it for the renderer's pixels), `WritePpm`/`WriteRtex`/`ReadRtex` use it, the `std::vector` buffers in the writers go away; callers in `rhi_demo`, `renderer_demo`, `rhi_bench` and `tests/rhi` follow.
- [x] `game/rhi_demo` (`std::unique_ptr<UniformRing>` becomes `UniquePtr`, `std::vector<unsigned char>` pixel buffers become `Vector<byte>`, `std::string` paths become `io::Path`), `game/rhi_bench` and `game/renderer_demo`/`renderer_bench`: no `std::` container, string or smart pointer left.
- [x] Anything the ports reveal as missing from the engine's containers (a `Vector::Assign(range)`, `Append(ByteSpan)`, `String::Reserve`, a `HashSet`, ...) is added with tests in this day, not worked around.
- [x] Tests: everything that passed before passes (the demo ctests, golden images byte for byte, `rtex` round trips); new cases for each container feature added; the Phase 9 file-layer tests with the span overloads.
- [x] **Documentation:** labels on the new overloads and container features; the guide pages (`io.md`, `foundation-containers.md`) updated; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` (foundation headers change: the whole suite), the asan and tsan flavors, and both demos 1000 frames with `--validation` on Wayland and X11; quote the lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`.
- [x] Update `STATUS.md`, the spec's inventory table (mark the rows done); commit.

**Done:** the engine and the game code spell bytes `byte`, `game/` contains no banned `std::` use, the file layer takes spans, and the full suite and both demos behave exactly as before.


## Result

- `unsigned char`/`uint8_t`..`int64_t` spellings in `engine/` and `game/` became `byte`/`u8`..`i64` (31 files); `game/` has no banned `std::` use (`textures.*` use `Vector<byte>`, `rhi_demo` uses `UniquePtr`, `rhi_bench`/`renderer_bench` use `Vector`, `Sort`, `Min`).
- New: `engine/foundation/algorithm.h` (`Min`, `Max`, `Clamp`, `IndexOf`, `Sort`, `StableSort`), `Append`/`Assign(Vector&, Span)` in `span.h`, `ByteSpan` overloads of `File::Read/Write`, `WriteFile`, `WriteFileAtomic`; `rhi::Span<T>` is now an alias of the foundation `Span<const T>` (`.data`/`.count` became `Data()`/`Size()`). Tests: `tests/types/algorithm_test.cpp`, a span case in `tests/io/file_test.cpp`.
- Docs: `io.md`, `foundation-containers.md`; lint 3515/3515; `ctest -L docs` passes.
- Runs: plain `ran 1149 of 3003 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 window test failed); asan `ran 832 of 3003`; tsan `ran 549 of 3003`. `rhi_demo` and `renderer_demo`, 1000 frames with `--validation` on Wayland and X11: exit 0, no validation messages.

## Post-implementation code review

`/code-review high`: 9 findings.
- Fixed: `Append` grows geometrically; `Append`/`Assign` assert the source does not overlap the vector; `IndexOf`/`Append`/`Assign` accept a `Vector` directly; `Clamp` is a template; `rhi_bench` fills with `memset`; the unreachable null-span checks in `rhi/desc.cpp` were removed (a null pointer with a size now aborts in the `Span` constructor); day record and STATUS written.
- Declined: always-checked indexing in RHI descriptor loops (one compare, same decision as Day 126); the `WriteFileAtomic(path, array, 0)` ambiguity (the pointer overload is the better match, so the call keeps its old meaning).
