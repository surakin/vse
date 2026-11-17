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
  wav.cpp  # Day 417
  asset_clip.cpp  # Day 418
  device.cpp  # Day 419
  audio_settings.cpp  # Day 419
  mixer.cpp  # Day 420
  mixer_buses.cpp  # Day 422
  limiter.cpp  # Day 422
  filter.cpp  # Day 423
  delay.cpp  # Day 424
  pcm_negotiate.cpp  # Day 425
  audio_system.cpp  # Day 427
  distance.cpp  # Day 428
  panning.cpp  # Day 429
  vorbis.cpp  # Day 432
  stream.cpp  # Day 435
  music.cpp  # Day 436
  spatial_audio.cpp  # Day 428
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND AUDIO_SOURCES linux/alsa_device.cpp linux/pipewire_device.cpp linux/linux_devices.cpp)  # Days 425-426
  # The PipeWire client headers (libpipewire is loaded at run time, never linked): optional, as shaders' slangc is.
  find_path(ENGINE_PIPEWIRE_INCLUDE pipewire/pipewire.h PATH_SUFFIXES pipewire-0.3)
  find_path(ENGINE_SPA_INCLUDE spa/param/audio/format-utils.h PATH_SUFFIXES spa-0.2)
endif()
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
    target_link_libraries(audio${suffix} PRIVATE vorbis${suffix})
    target_link_libraries(audio${suffix} PUBLIC asset${suffix} ecs${suffix} spatial${suffix} jobs${suffix} foundation_base${suffix})
  endforeach()
  return()
endif()

foreach(suffix "" "_asan" "_tsan")
  add_library(audio${suffix} STATIC ${AUDIO_SOURCES})
  target_include_directories(audio${suffix} PUBLIC ${AUDIO_INCLUDE_ROOT})
  if(ENGINE_PIPEWIRE_INCLUDE AND ENGINE_SPA_INCLUDE)
    target_include_directories(audio${suffix} SYSTEM PRIVATE ${ENGINE_PIPEWIRE_INCLUDE} ${ENGINE_SPA_INCLUDE})
    target_compile_definitions(audio${suffix} PRIVATE ENGINE_AUDIO_HAVE_PIPEWIRE=1)
  else()
    target_compile_definitions(audio${suffix} PRIVATE ENGINE_AUDIO_HAVE_PIPEWIRE=0)
  endif()
  if(CMAKE_DL_LIBS)
    target_link_libraries(audio${suffix} PUBLIC ${CMAKE_DL_LIBS})
  endif()
  target_compile_options(audio${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(audio${suffix} PRIVATE vorbis${suffix})
  target_link_libraries(audio${suffix} PUBLIC asset${suffix} ecs${suffix} spatial${suffix} jobs${suffix} foundation${suffix})
endforeach()
