# Phase 16 Days 211-214: the cstr module, the mem operations, the scalar math and the os console wrappers. Like tests/types, does not apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2 needs exceptions). Oracle files include the standard headers on purpose.
set(CSTR_TEST_SOURCES
  cstr_test.cpp
  math_test.cpp
  parse_double_test.cpp  # ParseDoubleExact's contract, and on Linux the macOS parser against std::from_chars (Phase 26 Day 284)
)
if(NOT ENGINE_MSVC_LIKE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
  list(APPEND CSTR_TEST_SOURCES os_console_test.cpp)  # redirects descriptors with POSIX calls; the Windows run covers the console wrappers through the ports
endif()

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_cstr_tests target foundation_lib suffix label)
  add_executable(${target} ${CSTR_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${foundation_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_cstr_tests(cstr_tests foundation_base "" "cstr")
if(ENGINE_HAVE_ASAN)
  engine_add_cstr_tests(cstr_tests_asan foundation_base_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)
  engine_add_cstr_tests(cstr_tests_tsan foundation_base_tsan " [tsan]" "tsan")
endif()
