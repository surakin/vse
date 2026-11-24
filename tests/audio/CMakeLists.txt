# The audio tests (Phase 26): the block hash and the render comparison (Day 415) and, from the next days, the module. Like tests/physics, they do not
# apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions); the library under test is built without. One executable per sanitizer flavor.
set(AUDIO_TEST_SOURCES
  block_hash_test.cpp  # Day 415
  render_compare_test.cpp  # Day 415
  repeatable_render_test.cpp  # Day 415
  format_test.cpp  # Day 416
  convert_test.cpp  # Day 416
  clip_test.cpp  # Day 416
  wav_test.cpp  # Day 417
  aclip_test.cpp  # Day 418
  device_test.cpp  # Day 419
  mixer_test.cpp  # Day 420
  control_test.cpp
  bus_test.cpp  # Day 422
  effect_test.cpp  # Days 423-424
  backend_test.cpp  # Days 425-426
  system_test.cpp  # Day 427
  spatial_test.cpp  # Day 428
  panning_test.cpp  # Day 429
  doppler_test.cpp  # Day 430
  occlusion_test.cpp  # Day 431
  vorbis_test.cpp  # Day 432
  vorbis_decode_test.cpp  # Day 433
  vorbis_seek_test.cpp  # Day 434
  vorbis_clip_test.cpp  # Day 434
  stream_test.cpp  # Day 435
  music_test.cpp  # Day 436
  snapshot_test.cpp  # Day 437
  stress_test.cpp  # Day 442
  hardening_test.cpp  # the closing review's findings
  alloc_test.cpp  # the audio thread's no-allocation rule, counted
  ${CMAKE_SOURCE_DIR}/tools/asset_mint/audio_source.cpp  # Day 418: the audio minter under test
)

# LeakSanitizer's suppressions (the PipeWire client library's own allocations): Linux only.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND AUDIO_TEST_SOURCES lsan_options.cpp)
endif()

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_audio_tests target audio_lib suffix label)
  add_executable(${target} ${AUDIO_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${audio_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/tools/asset_mint ${CMAKE_SOURCE_DIR}/tests/support)
  target_compile_definitions(${target} PRIVATE ENGINE_AUDIO_LSAN_SUPP="${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp"
                                              ENGINE_AUDIO_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
                                              ENGINE_AUDIO_TMP="${CMAKE_CURRENT_BINARY_DIR}")
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
  # The time bound of the voice-cost test (tests/support/soak.h): the plain flavor only.
  if(label STREQUAL "audio")
    engine_soak_catch_test(${target} "Snapshot: the cost of a voice at each quality, with and without effects"
      SOURCE tests/audio/snapshot_test.cpp)
  endif()
endfunction()

engine_add_audio_tests(audio_tests audio "" "audio")
if(ENGINE_HAVE_ASAN)
  engine_add_audio_tests(audio_tests_asan audio_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_audio_tests(audio_tests_tsan audio_tsan " [tsan]" "tsan")
endif()

# The backend checklist gate (CMake script, like io_backend_lists): BACKEND.md matches the Backend: labels of the audio headers.
add_test(NAME audio_backend_lists COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_audio_backend_lists.cmake)
add_test(NAME audio_backend_lists_planted COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
         -DBACKEND_FILE=${CMAKE_SOURCE_DIR}/tests/io/cross_syntax/planted_backend.md -P ${CMAKE_CURRENT_SOURCE_DIR}/check_audio_backend_lists.cmake)
set_tests_properties(audio_backend_lists_planted PROPERTIES WILL_FAIL TRUE)
engine_label_tests(audio_backend_lists audio_backend_lists_planted)
