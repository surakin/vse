# stb_image's JPEG decoder (https://github.com/nothings/stb, v2.32), vendored: see VENDORED.md and PLAN.md assumption #19. One static C library per flavor
# (`stb_jpeg`, `stb_jpeg_asan`, `stb_jpeg_tsan`). Only engine/asset_import/image_decode.cpp includes stb_image.h.
cmake_minimum_required(VERSION 3.20)

function(engine_add_stb_jpeg name)
  add_library(${name} STATIC stb_image_impl.c)
  target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(${name} PUBLIC STBI_ONLY_JPEG STBI_NO_STDIO STBI_NO_SIMD STBI_NO_LINEAR STBI_NO_HDR STBI_MAX_DIMENSIONS=16384)
  set_target_properties(${name} PROPERTIES UNITY_BUILD OFF POSITION_INDEPENDENT_CODE ON)
  if(MSVC AND NOT ENGINE_CLANG_CL)
    target_compile_options(${name} PRIVATE /w)
  else()
    # -fwrapv: the integer IDCT of a corrupt file overflows `int` (stb_image.h `stbi__idct_block`: the dequantized
    # coefficients are 16-bit but their products and sums are not bounded). Two's complement wrap is the intended and the
    # only result on every machine we build for; with the flag it is defined, so the output is the same at every
    # optimization level and UBSan has nothing to report. The header stays unmodified.
    target_compile_options(${name} PRIVATE -w -fwrapv)
  endif()
endfunction()

engine_add_stb_jpeg(stb_jpeg)
if(ENGINE_HAVE_ASAN)
  engine_add_stb_jpeg(stb_jpeg_asan)
  target_compile_options(stb_jpeg_asan PRIVATE ${ENGINE_ASAN_COMPILE_OPTIONS})
endif()
if(ENGINE_HAVE_TSAN)
  engine_add_stb_jpeg(stb_jpeg_tsan)
  target_compile_options(stb_jpeg_tsan PRIVATE ${ENGINE_TSAN_COMPILE_OPTIONS})
endif()
