# Vendored libpng

Upstream: http://www.libpng.org/pub/png/libpng.html. Pinned release: **1.6.44**, tarball `libpng-1.6.44.tar.gz`
(sha256 `8c25a7792099a0089fa1cc76c94260d0bb3f1ec52b93671b572f8bb61577b732`), fetched 2026-11-15.
License: `LICENSE` (the PNG Reference Library License version 2), unmodified (sha256 `7317e078e2d3b5d7ba5a6159e650945153262b44b76f6700f8e9edb261c5143e`).
The sources are the upstream files, unmodified: `png*.c` and `png.h pngconf.h pngdebug.h pnginfo.h pngpriv.h pngstruct.h`; `pnglibconf.h` is upstream's
`scripts/pnglibconf.h.prebuilt`, unmodified. Not vendored: the hardware-optimized filter directories (`arm`, `intel`, `mips`, `powerpc`, `loongarch`),
the examples, tests and build systems.

Per `PLAN.md` assumptions #17 and #19 (amended 2026-11-14) libpng decodes PNG for `engine/asset_import` (the glTF importer, Phase 22 Day 338; its input
may be hostile, so it fails by return value: `engine/asset_import/image_decode.cpp` keeps the `setjmp` of libpng's error path inside the wrapper and sets
the size limits before decoding) and then reads the PNG strikes of FreeType's color bitmap fonts (Phase 27 Day 369). Only `image_decode.cpp` includes
`png.h`.

Built by `CMakeLists.txt` as one static C library per flavor (`png`, `png_asan`, `png_tsan`) over the vendored zlib; the SIMD filter paths are switched off
(`PNG_ARM_NEON_OPT=0` and the like), so one portable C code path decodes the same bytes everywhere. Warnings are off for the target (third-party C).
