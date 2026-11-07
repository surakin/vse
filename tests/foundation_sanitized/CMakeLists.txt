# Foundation tests that only mean something under a sanitizer (data races in the shared containers). One executable per flavor, as in tests/settings.
# Deliberately does NOT apply ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are exception-based.
set(FOUNDATION_SANITIZED_SOURCES
  shared_ptr_threads_test.cpp
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_foundation_sanitized_tests target foundation_lib suffix label)
  add_executable(${target} ${FOUNDATION_SANITIZED_SOURCES})
  target_link_libraries(${target} PRIVATE ${foundation_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_foundation_sanitized_tests(foundation_sanitized_tests foundation_base "" "foundation")
if(ENGINE_HAVE_ASAN)
  engine_add_foundation_sanitized_tests(foundation_sanitized_tests_asan foundation_base_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_foundation_sanitized_tests(foundation_sanitized_tests_tsan foundation_base_tsan " [tsan]" "tsan")
endif()
