# The text layer's tests (Phase 14). Like tests/types, they do not apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions); the
# library under test is built without. One executable per sanitizer flavor.
set(TEXT_TEST_SOURCES
  library_test.cpp
  font_test.cpp
  glyph_atlas_test.cpp
  layout_test.cpp
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_text_tests target text_lib suffix label)
  add_executable(${target} ${TEXT_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${text_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(${target} PRIVATE TEXT_TEST_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden" TEXT_TEST_FONT_FILE="${CMAKE_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf")
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_text_tests(text_tests text "" "text")
if(ENGINE_HAVE_ASAN)
  engine_add_text_tests(text_tests_asan text_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_text_tests(text_tests_tsan text_tsan " [tsan]" "tsan")
endif()
