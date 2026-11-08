# FreeType (https://freetype.org), vendored as source: see VENDORED.md and PLAN.md assumption #17. Upstream's own CMake and autotools builds are not
# used; this file builds the engine's subset (TrueType and CFF outlines, the smooth rasterizer, the auto-hinter; the module list is
# include/freetype/config/ftmodule.h) into one static library, `freetype`, compiled with plain flags and linked into the plain, ASan and TSan
# flavors alike. Each module compiles through its single-file aggregator (base/ftbase.c, sfnt/sfnt.c, ...), as FreeType's docs/INSTALL.ANY
# describes, so there is no unity-build question here.
cmake_minimum_required(VERSION 3.20)

add_library(freetype STATIC
  src/base/ftsystem.c
  src/base/ftinit.c
  src/base/ftdebug.c
  src/base/ftbase.c
  src/base/ftbbox.c
  src/base/ftglyph.c
  src/base/ftbitmap.c
  src/base/ftgasp.c
  src/base/ftfstype.c
  src/base/fttype1.c
  src/base/ftmm.c
  src/base/ftcolor.c
  src/sfnt/sfnt.c
  src/truetype/truetype.c
  src/cff/cff.c
  src/psaux/psaux.c
  src/pshinter/pshinter.c
  src/psnames/psnames.c
  src/smooth/smooth.c
  src/autofit/autofit.c)
target_compile_definitions(freetype PRIVATE FT2_BUILD_LIBRARY)
# The options header the engine uses (upstream's, then the options switched off): every target that includes FreeType must see the same one.
target_compile_definitions(freetype PUBLIC "FT_CONFIG_OPTIONS_H=\"engine_ftoption.h\"")
target_include_directories(freetype PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/engine_config)
set_target_properties(freetype PROPERTIES C_STANDARD 99 UNITY_BUILD OFF POSITION_INDEPENDENT_CODE ON)
# A third-party library: its warnings are not ours to fix (CLAUDE.md "Warnings are errors"; the root adds -Werror after third_party/).
if(MSVC OR CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  target_compile_options(freetype PRIVATE /W0)
else()
  target_compile_options(freetype PRIVATE -w)
endif()
