# zlib 1.3.1 (https://zlib.net), vendored: see VENDORED.md and PLAN.md assumptions #17 and #19. One static C library per flavor (`zlib`, `zlib_asan`,
# `zlib_tsan`). Used by libpng (the PNG decoder of the glTF importer, then FreeType's color bitmap strikes), reached only through that.
cmake_minimum_required(VERSION 3.20)

function(engine_add_zlib name)
  add_library(${name} STATIC adler32.c crc32.c deflate.c inffast.c inflate.c inftrees.c trees.c zutil.c)
  target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  set_target_properties(${name} PROPERTIES UNITY_BUILD OFF POSITION_INDEPENDENT_CODE ON)
  if(NOT MSVC OR ENGINE_CLANG_CL)
    target_compile_definitions(${name} PRIVATE Z_HAVE_UNISTD_H=1)
  endif()
  # Z_SOLO: no gz* files and no default allocator; libpng (and FreeType) hand zlib their own allocation functions.
  target_compile_definitions(${name} PRIVATE Z_SOLO=1)
  # Third-party C: its warnings are not ours to fix. Warnings off for this target only.
  if(MSVC AND NOT ENGINE_CLANG_CL)
    target_compile_options(${name} PRIVATE /w)
  else()
    target_compile_options(${name} PRIVATE -w)
  endif()
endfunction()

engine_add_zlib(zlib)
if(ENGINE_HAVE_ASAN)
  engine_add_zlib(zlib_asan)
  target_compile_options(zlib_asan PRIVATE ${ENGINE_ASAN_COMPILE_OPTIONS})
endif()
if(ENGINE_HAVE_TSAN)
  engine_add_zlib(zlib_tsan)
  target_compile_options(zlib_tsan PRIVATE ${ENGINE_TSAN_COMPILE_OPTIONS})
endif()
