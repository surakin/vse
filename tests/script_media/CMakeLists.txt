# The script media tests (Phase 27 Day 468): the audio and input bindings over a real mixer and a real runtime. Like
# tests/script, they do not apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions). One executable per
# sanitizer flavor.
if(NOT TARGET luajit)
  return()  # LuaJIT builds on Linux and Windows x86-64 so far (macOS: Day 474)
endif()

set(SCRIPT_MEDIA_TEST_SOURCES
  script_media_test.cpp  # Day 468
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_script_media_tests target lib foundation_lib asset_lib suffix label)
  add_executable(${target} ${SCRIPT_MEDIA_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${lib} ${asset_lib} ${foundation_lib} Catch2::Catch2WithMain Threads::Threads)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_script_media_tests(script_media_tests script_media foundation asset "" "script_media")
if(ENGINE_HAVE_ASAN)
  engine_add_script_media_tests(script_media_tests_asan script_media_asan foundation_asan asset_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_script_media_tests(script_media_tests_tsan script_media_tsan foundation_tsan asset_tsan " [tsan]" "tsan")
endif()
