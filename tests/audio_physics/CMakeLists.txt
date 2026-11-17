# The audio_physics tests (Phase 26 Day 431): the occlusion query over a real physics world. Like tests/audio, they do not apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions). One executable per sanitizer flavor.
set(AUDIO_PHYSICS_TEST_SOURCES
  physics_occlusion_test.cpp  # Day 431
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_audio_physics_tests target lib suffix label)
  add_executable(${target} ${AUDIO_PHYSICS_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${lib} Catch2::Catch2WithMain Threads::Threads)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_audio_physics_tests(audio_physics_tests audio_physics "" "audio_physics")
if(ENGINE_HAVE_ASAN)
  engine_add_audio_physics_tests(audio_physics_tests_asan audio_physics_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_audio_physics_tests(audio_physics_tests_tsan audio_physics_tsan " [tsan]" "tsan")
endif()
