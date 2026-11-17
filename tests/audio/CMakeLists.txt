# The audio tests (Phase 26): the block hash and the render comparison (Day 415) and, from the next days, the module. Like tests/physics, they do not
# apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions); the library under test is built without. One executable per sanitizer flavor.
set(AUDIO_TEST_SOURCES
  block_hash_test.cpp  # Day 415
  render_compare_test.cpp  # Day 415
  repeatable_render_test.cpp  # Day 415
  format_test.cpp  # Day 416
  convert_test.cpp  # Day 416
  clip_test.cpp  # Day 416
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_audio_tests target audio_lib suffix label)
  add_executable(${target} ${AUDIO_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${audio_lib} Catch2::Catch2WithMain Threads::Threads)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_audio_tests(audio_tests audio "" "audio")
if(ENGINE_HAVE_ASAN)
  engine_add_audio_tests(audio_tests_asan audio_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_audio_tests(audio_tests_tsan audio_tsan " [tsan]" "tsan")
endif()
