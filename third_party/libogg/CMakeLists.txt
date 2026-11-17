# libogg 1.3.6 (https://xiph.org/ogg/), vendored: see VENDORED.md and PLAN.md assumption #30. One static C library per flavor (`ogg`, `ogg_asan`,
# `ogg_tsan`). Reached only through the audio module's Vorbis facade (engine/audio/vorbis.cpp), over libvorbis.
cmake_minimum_required(VERSION 3.20)

function(engine_add_ogg name)
  add_library(${name} STATIC bitwise.c framing.c)
  target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  set_target_properties(${name} PROPERTIES UNITY_BUILD OFF POSITION_INDEPENDENT_CODE ON)
  # Third-party C: its warnings are not ours to fix. Warnings off for this target only.
  if(MSVC AND NOT ENGINE_CLANG_CL)
    target_compile_options(${name} PRIVATE /w)
  else()
    target_compile_options(${name} PRIVATE -w)
  endif()
endfunction()

engine_add_ogg(ogg)
if(ENGINE_HAVE_ASAN)
  engine_add_ogg(ogg_asan)
  target_compile_options(ogg_asan PRIVATE ${ENGINE_ASAN_COMPILE_OPTIONS})
endif()
if(ENGINE_HAVE_TSAN)
  engine_add_ogg(ogg_tsan)
  target_compile_options(ogg_tsan PRIVATE ${ENGINE_TSAN_COMPILE_OPTIONS})
endif()
