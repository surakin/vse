# Phase 27 Day 468: the audio and input bindings of the script module. Layer 14, above script, audio and the platform's
# input types (docs/superpowers/specs/2026-11-18-scripting-design.md): `script` never includes `audio` or a platform
# header. Headers are included as "script_media/<name>.h", rooted at engine/. Linux and Windows (Day 473); macOS
# on Day 474, like script. One library per sanitizer flavor.
set(SCRIPT_MEDIA_SOURCES
  audio_services.cpp  # Day 468
  audio_bindings.cpp  # Day 468
  input_snapshot.cpp  # Day 468
  input_bindings.cpp  # Day 468
  replay_log.cpp  # Day 469
)
set(SCRIPT_MEDIA_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Windows has no ThreadSanitizer, and ASan only where the compiler has it (as the modules below this one).
set(_script_media_flavors "_asan" "_tsan")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_script_media_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _script_media_flavors "_asan")
  endif()
endif()
foreach(suffix "" ${_script_media_flavors})
  add_library(script_media${suffix} STATIC ${SCRIPT_MEDIA_SOURCES})
  target_include_directories(script_media${suffix} PUBLIC ${SCRIPT_MEDIA_INCLUDE_ROOT})
  target_compile_options(script_media${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(script_media${suffix} PUBLIC script${suffix} audio${suffix} platform_input)
endforeach()
