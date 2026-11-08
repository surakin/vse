/*
 * The engine's FreeType options: upstream's include/freetype/config/ftoption.h, unmodified, then the options the engine turns off
 * (see VENDORED.md). Selected with FT_CONFIG_OPTIONS_H in third_party/freetype/CMakeLists.txt for every target that includes FreeType.
 */
#include <freetype/config/ftoption.h>

/* No gzip/zlib (WOFF 1 fonts, compressed SVG documents): the engine loads TrueType and OpenType files. */
#undef FT_CONFIG_OPTION_USE_ZLIB
#undef FT_CONFIG_OPTION_SYSTEM_ZLIB

/* No SVG glyphs (the SVG table is out of scope, Phase 16 plan). */
#undef FT_CONFIG_OPTION_SVG
