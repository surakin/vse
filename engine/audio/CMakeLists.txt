# Phase 26: the audio module (CPU only, portable): the mixer, voices, buses and the devices. Layer 13, beside physics and asset (docs/superpowers/specs/
# 2026-11-17-audio-design.md). Headers are included as "audio/<name>.h", rooted at engine/. Built plain, ASan and TSan from one source list, each against
# the matching foundation variant, like spatial. No special floating-point flags: the project's (no contraction, no fast-math) apply, and one build renders
# the same bits run to run; across machines output is compared within a tolerance (audio/render_compare.h).
set(AUDIO_SOURCES
  block_hash.cpp  # Day 415
  render_compare.cpp  # Day 415
  format.cpp  # Day 416
  convert.cpp  # Day 416
  clip.cpp  # Day 416
)
set(AUDIO_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_audio_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _audio_flavors "_asan")
  endif()
  foreach(suffix "" ${_audio_flavors})
    add_library(audio${suffix} STATIC ${AUDIO_SOURCES})
    target_include_directories(audio${suffix} PUBLIC ${AUDIO_INCLUDE_ROOT})
    target_compile_options(audio${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
    target_link_libraries(audio${suffix} PUBLIC foundation_base${suffix})
  endforeach()
  return()
endif()

foreach(suffix "" "_asan" "_tsan")
  add_library(audio${suffix} STATIC ${AUDIO_SOURCES})
  target_include_directories(audio${suffix} PUBLIC ${AUDIO_INCLUDE_ROOT})
  target_compile_options(audio${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(audio${suffix} PUBLIC foundation${suffix})
endforeach()
