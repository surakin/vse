# libvorbis 1.3.7 (https://xiph.org/vorbis/), vendored: see VENDORED.md and PLAN.md assumption #30. One static C library per flavor (`vorbis`,
# `vorbis_asan`, `vorbis_tsan`) over the vendored libogg: the decoder and the code it shares with the encoder's analysis (`vorbisenc` and `vorbisfile`
# are not vendored). Only engine/audio/vorbis.cpp includes its headers. The sanitized flavors leave UBSan out (Day 414 spike: benign signed left
# shifts in sharedbook.c on valid streams) and keep AddressSanitizer.
cmake_minimum_required(VERSION 3.20)

function(engine_add_vorbis name ogg_target)
  add_library(${name} STATIC mdct.c smallft.c block.c envelope.c window.c lsp.c lpc.c analysis.c synthesis.c psy.c info.c floor1.c floor0.c
                             res0.c mapping0.c registry.c codebook.c sharedbook.c lookup.c bitrate.c)
  target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PUBLIC ${ogg_target})
  if(UNIX)
    target_link_libraries(${name} PUBLIC m)
  endif()
  set_target_properties(${name} PROPERTIES UNITY_BUILD OFF POSITION_INDEPENDENT_CODE ON)
  # Third-party C: its warnings are not ours to fix. Warnings off for this target only.
  if(MSVC AND NOT ENGINE_CLANG_CL)
    target_compile_options(${name} PRIVATE /w)
  else()
    target_compile_options(${name} PRIVATE -w)
  endif()
endfunction()

engine_add_vorbis(vorbis ogg)
if(ENGINE_HAVE_ASAN)
  engine_add_vorbis(vorbis_asan ogg_asan)
  target_compile_options(vorbis_asan PRIVATE ${ENGINE_ASAN_COMPILE_OPTIONS})
  if(NOT MSVC)
    target_compile_options(vorbis_asan PRIVATE -fno-sanitize=undefined)
  endif()
endif()
if(ENGINE_HAVE_TSAN)
  engine_add_vorbis(vorbis_tsan ogg_tsan)
  target_compile_options(vorbis_tsan PRIVATE ${ENGINE_TSAN_COMPILE_OPTIONS})
endif()
