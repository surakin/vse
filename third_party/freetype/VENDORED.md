# Vendored FreeType

Upstream: https://freetype.org (source: https://download.savannah.gnu.org/releases/freetype/)
Pinned release: **FreeType 2.15.0**, `freetype-2.15.0.tar.xz`, SHA-256 **`93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5`**
(downloaded 2026-11-08; the detached signature `.sig` was fetched with it but not verified: the hash above is what the repository pins).

Per `PLAN.md` assumption #17 this is a source vendor (not a submodule, not `FetchContent`, not the distro's `libfreetype.so`), so builds stay offline
and reproducible on every platform the roadmap names.

## License

The **FreeType License (FTL)**, `docs/FTL.TXT` (BSD-style; a product that uses FreeType must credit it in its documentation, with a sentence such as "Portions of this software are copyright (c) <year> The FreeType Project (www.freetype.org). All rights reserved."). **Open:** the engine has no shipped-notices file yet; the credit goes into the one the first release adds (filed in `KNOWN-ISSUES.md`). The GPLv2
alternative upstream offers is not used. `LICENSE.TXT` is upstream's statement of the two options.

## Kept from the upstream archive

`include/` (all headers), `LICENSE.TXT`, `README`, `docs/FTL.TXT`, and the sources of the modules the engine builds, copied unmodified:
`src/base` (the base object, system, init, debug, glyph, bitmap, bbox, multiple-master and color files), `src/sfnt`, `src/truetype`, `src/cff`, `src/psaux`,
`src/pshinter`, `src/psnames`, `src/smooth`, `src/autofit`. Dropped: every other driver and renderer, the cache, the compressed-font and validation
modules, upstream's own build files, the docs, tests and the build scripts.

## Two files are the engine's, not upstream's

- `include/freetype/config/ftmodule.h` is the file FreeType's build *generates* from `modules.cfg`; it holds the engine's module list (TrueType, CFF,
  psaux, psnames, pshinter, sfnt, the smooth rasterizer, the auto-hinter).
- `engine_config/engine_ftoption.h` includes upstream's `ftoption.h` unmodified and then undefines `FT_CONFIG_OPTION_USE_ZLIB`, `_SYSTEM_ZLIB` and
  `FT_CONFIG_OPTION_SVG`. `CMakeLists.txt` selects it with `FT_CONFIG_OPTIONS_H` as a PUBLIC definition, so every target that includes FreeType sees the same
  options.

## Build

`CMakeLists.txt` builds one static library, `freetype`, from the modules' single-file aggregators (`base/ftbase.c`, `sfnt/sfnt.c`, ...) with
`FT2_BUILD_LIBRARY`, warnings off (a third-party library: the root's `-Werror` is added after `third_party/`, and this target also passes `-w`/`/W0`).
It builds on Linux (Clang, GCC); the Windows PC checks MSVC and clang-cl.

## Updating

Download the new release, check its hash, replace `include/` and the kept `src/` directories, re-apply the two engine files above, update the version and
hash here and in `PLAN.md` assumption #17, and run the text tests (`tests/text/library_test.cpp` checks the version).
