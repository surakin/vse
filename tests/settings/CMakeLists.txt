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

engine_add_settings_tests(settings_tests foundation "" "settings")
engine_add_settings_tests(settings_tests_asan foundation_asan " [asan]" "asan")
engine_add_settings_tests(settings_tests_tsan foundation_tsan " [tsan]" "tsan")
