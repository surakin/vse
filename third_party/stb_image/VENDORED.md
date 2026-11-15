# Vendored stb_image (JPEG only)

Upstream: https://github.com/nothings/stb, `stb_image.h` **v2.32** (public domain, or MIT where public domain is not recognised: the license text is at the
end of the file), fetched 2026-11-15, unmodified (sha256 `deb4f18c9ca21b751a1323019031c94c0bf5ca1d2929ef5913cc56f00aab6e91`). `stb_image_impl.c` is ours: it
defines the library's switches and includes the header once.

## Why this decoder (the Phase 22 Day 338 spike)

A baseline JPEG decoder was needed for the images of glTF files (the host fetches files from rooms: the input is hostile). Candidates: stb_image's JPEG
path, libjpeg-turbo.

| | stb_image (JPEG only) | libjpeg-turbo |
|---|---|---|
| Size | one header, about 4,000 lines compiled with `STBI_ONLY_JPEG` | about 150 files, SIMD assembly optional |
| License | public domain / MIT | IJG, BSD-3, zlib |
| Progressive, baseline, CMYK/Adobe | yes (8-bit); no 12-bit, no arithmetic coding | all |
| Determinism | integer IDCT, upsampling and color conversion; `STBI_NO_SIMD` takes the scalar paths: the same pixels on every machine | the IDCT method is a build option; SIMD and C paths are meant to agree |
| Failure | returns null, never exits; a thread-local reason | `longjmp` through an error manager |
| Security history | several fuzzer findings fixed over the years; its own header says so (a "SECURITY NOTE": do not use it on untrusted input without precautions) | a long history of fixes, and an active fuzzing project |
| Memory | allocates the output and the per-component buffers sized by the header's dimensions | the same |

**Decision: stb_image's JPEG path**, behind the wrapper's precautions: `STBI_MAX_DIMENSIONS=16384`, the dimensions and the pixel count are read with
`stbi_info_from_memory` and checked against the import limits before any decode, its asserts compile to nothing (`STBI_ASSERT`: a hostile image must not
abort), and the decoder is exercised by a fuzz-style test of mutated and truncated JPEG files under AddressSanitizer. **Found by the Day 338 tests:** a quality-95 progressive file (4:2:0) made by libjpeg's `cjpeg` fails with `bad huffman code` while the same image at quality 75 decodes; libjpeg-turbo stays the fallback
(a new `KNOWN-ISSUES.md` entry would say why) if a finding makes stb unfit. The loser is recorded in the Day 338 spec section.

## How it is built (`CMakeLists.txt`)

One static C library per flavor (`stb_jpeg`, `stb_jpeg_asan`, `stb_jpeg_tsan`): `STBI_ONLY_JPEG` (no other format), `STBI_NO_STDIO` (**no `fopen`**: all file
access goes through `io::`), `STBI_NO_SIMD`, `STBI_NO_LINEAR`, `STBI_NO_HDR`, `STBI_MAX_DIMENSIONS=16384`. Only `engine/asset_import/image_decode.cpp`
includes `stb_image.h`.
