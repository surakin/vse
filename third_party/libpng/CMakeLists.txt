# libpng 1.6.44 (http://www.libpng.org/pub/png/libpng.html), vendored: see VENDORED.md and PLAN.md assumptions #17 and #19. One static C library per
# flavor (`png`, `png_asan`, `png_tsan`), built over the vendored zlib. The hardware-optimized filters are off (the portable C paths decode the same
# bytes on every machine). Only engine/asset_import/image_decode.cpp includes png.h.
cmake_minimum_required(VERSION 3.20)

function(engine_add_png name zlib_target)
  add_library(${name} STATIC png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c pngrio.c pngrtran.c pngrutil.c pngset.c pngtrans.c
                             pngwio.c pngwrite.c pngwtran.c pngwutil.c)
  target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PUBLIC ${zlib_target})
  target_compile_definitions(${name} PRIVATE PNG_ARM_NEON_OPT=0 PNG_INTEL_SSE_OPT=0 PNG_MIPS_MSA_OPT=0 PNG_POWERPC_VSX_OPT=0
                                             PNG_LOONGARCH_LSX_OPT=0)
  set_target_properties(${name} PROPERTIES UNITY_BUILD OFF POSITION_INDEPENDENT_CODE ON)
  if(MSVC AND NOT ENGINE_CLANG_CL)
    target_compile_options(${name} PRIVATE /w)
  else()
    target_compile_options(${name} PRIVATE -w)
  endif()
endfunction()

engine_add_png(png zlib)
if(ENGINE_HAVE_ASAN)
  engine_add_png(png_asan zlib_asan)
  target_compile_options(png_asan PRIVATE ${ENGINE_ASAN_COMPILE_OPTIONS})
endif()
if(ENGINE_HAVE_TSAN)
  engine_add_png(png_tsan zlib_tsan)
  target_compile_options(png_tsan PRIVATE ${ENGINE_TSAN_COMPILE_OPTIONS})
endif()
