# The UI toolkit's tests (Phase 15). Like tests/text, they do not apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions); the library under
# test is built without. One executable per sanitizer flavor.
set(UI_TEST_SOURCES
  version_test.cpp
  canvas_test.cpp
  draw_list_test.cpp
  theme_test.cpp
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_ui_tests target ui_lib suffix label)
  add_executable(${target} ${UI_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${ui_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(${target} PRIVATE UI_TEST_FONT_FILE="${CMAKE_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf")
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_ui_tests(ui_tests ui "" "ui")
if(ENGINE_HAVE_ASAN)
  engine_add_ui_tests(ui_tests_asan ui_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_ui_tests(ui_tests_tsan ui_tsan " [tsan]" "tsan")
endif()
