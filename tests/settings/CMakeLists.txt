# Like /tests/types, deliberately does NOT apply ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are exception-based.
# One executable per sanitizer flavor, as in tests/io.
set(SETTINGS_TEST_SOURCES
  key_test.cpp
  value_test.cpp
  registry_test.cpp
  config_store_test.cpp
  config_text_test.cpp
  file_store_test.cpp
  layers_test.cpp
  settings_test.cpp
  migration_test.cpp
  engine_settings_test.cpp
  review_test.cpp
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_settings_tests target foundation_lib suffix label)
  add_executable(${target} ${SETTINGS_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${foundation_lib} Catch2::Catch2WithMain Threads::Threads)
  target_compile_definitions(${target} PRIVATE TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/support ${CMAKE_SOURCE_DIR}/tests/io)
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_settings_tests(settings_tests foundation_settings "" "settings")
engine_add_settings_tests(settings_tests_asan foundation_settings_asan " [asan]" "asan")
engine_add_settings_tests(settings_tests_tsan foundation_settings_tsan " [tsan]" "tsan")

# The backend checklist stays in step with the headers, and the neutral headers compile for the Windows and macOS targets (needs clang++; skips without it).
add_test(NAME settings_backend_lists COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_settings_backend_lists.cmake)
function(engine_settings_cross_syntax_test name triple)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DTRIPLE=${triple} -DROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cross_syntax/check_settings_cross_syntax.cmake)
endfunction()
engine_settings_cross_syntax_test(settings_cross_syntax_windows x86_64-pc-windows-msvc)
engine_settings_cross_syntax_test(settings_cross_syntax_macos arm64-apple-macos13)
add_test(NAME settings_backend_lists_planted COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
         -DBACKEND_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cross_syntax/planted_backend.md -P ${CMAKE_CURRENT_SOURCE_DIR}/check_settings_backend_lists.cmake)
set_tests_properties(settings_backend_lists_planted PROPERTIES WILL_FAIL TRUE)
engine_label_tests(settings_backend_lists settings_backend_lists_planted settings_cross_syntax_windows settings_cross_syntax_macos)
