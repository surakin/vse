# Phase 26 Day 431: the physics implementation of the audio occlusion query. Layer 14, above audio and physics (docs/superpowers/specs/
# 2026-11-17-audio-design.md): the one target that names both. Headers are included as "audio_physics/<name>.h", rooted at engine/.
set(AUDIO_PHYSICS_SOURCES
  physics_occlusion.cpp  # Day 431
)
set(AUDIO_PHYSICS_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_audio_physics_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _audio_physics_flavors "_asan")
  endif()
  foreach(suffix "" ${_audio_physics_flavors})
    add_library(audio_physics${suffix} STATIC ${AUDIO_PHYSICS_SOURCES})
    target_include_directories(audio_physics${suffix} PUBLIC ${AUDIO_PHYSICS_INCLUDE_ROOT})
    target_compile_options(audio_physics${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
    target_link_libraries(audio_physics${suffix} PUBLIC audio${suffix} physics${suffix})
  endforeach()
  return()
endif()

foreach(suffix "" "_asan" "_tsan")
  add_library(audio_physics${suffix} STATIC ${AUDIO_PHYSICS_SOURCES})
  target_include_directories(audio_physics${suffix} PUBLIC ${AUDIO_PHYSICS_INCLUDE_ROOT})
  target_compile_options(audio_physics${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(audio_physics${suffix} PUBLIC audio${suffix} physics${suffix})
endforeach()
