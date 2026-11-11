# Like /tests/foundation, deliberately does NOT apply ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are
# exception-based. The foundation library under test is still built exception/RTTI-free. One executable per sanitizer
# flavor, as in tests/jobs.
set(IO_TEST_SOURCES
  path_test.cpp
  file_system_test.cpp
  file_test.cpp
  directory_test.cpp
  async_file_test.cpp
  share_test.cpp
)

find_package(Threads REQUIRED)
include(Catch)

# Windows (Phase 13): the whole suite (async_file_test with the job system from Day 153) and the Windows-only cases.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_io_windows_sources path_test.cpp file_system_test.cpp file_test.cpp directory_test.cpp async_file_test.cpp share_test.cpp io_windows_test.cpp)
  set(_io_windows_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _io_windows_flavors "_asan")
  endif()
  foreach(suffix "" ${_io_windows_flavors})
    add_executable(io_tests${suffix} ${_io_windows_sources})
    target_link_libraries(io_tests${suffix} PRIVATE jobs${suffix} foundation_io${suffix} Catch2::Catch2WithMain)
    target_include_directories(io_tests${suffix} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/support)
    engine_asan_runtime(io_tests${suffix})
  endforeach()
  engine_discover_tests(io_tests TEST_SUFFIX "" PROPERTIES LABELS "io")
  if(ENGINE_HAVE_ASAN)
    engine_discover_tests(io_tests_asan TEST_SUFFIX " [asan]" PROPERTIES LABELS "asan")
  endif()
  # The backend checklist gate is a CMake script and runs here too (BACKEND.md gains the Windows notes from Day 151); the cross-syntax
  # checks stay Linux-only, the Windows build being the real check of the neutral headers.
  add_test(NAME io_backend_lists COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_io_backend_lists.cmake)
  add_test(NAME io_backend_lists_planted COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
           -DBACKEND_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cross_syntax/planted_backend.md -P ${CMAKE_CURRENT_SOURCE_DIR}/check_io_backend_lists.cmake)
  set_tests_properties(io_backend_lists_planted PROPERTIES WILL_FAIL TRUE)
  engine_label_tests(io_backend_lists io_backend_lists_planted)
  return()
endif()

function(engine_add_io_tests target foundation_lib jobs_lib suffix label)
  add_executable(${target} ${IO_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${jobs_lib} ${foundation_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/support)
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_io_tests(io_tests foundation_io jobs "" "io")
engine_add_io_tests(io_tests_asan foundation_io_asan jobs_asan " [asan]" "asan")
engine_add_io_tests(io_tests_tsan foundation_io_tsan jobs_tsan " [tsan]" "tsan")

# Phase 9 Day 124: the backend checklist stays in step with the headers, and the neutral headers compile for the Windows and macOS targets
# (needs clang++; skips without it).
add_test(NAME io_backend_lists COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_io_backend_lists.cmake)
function(engine_io_cross_syntax_test name triple)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DTRIPLE=${triple} -DROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cross_syntax/check_io_cross_syntax.cmake)
endfunction()
engine_io_cross_syntax_test(io_cross_syntax_windows x86_64-pc-windows-msvc)
# Kept now that macOS builds for real (Phase 26 Day 286): the Intel development Mac builds x86-64 only, so this is what compiles these
# headers for arm64 (Apple silicon) until Phase 26 Day 310's arm64 build.
engine_io_cross_syntax_test(io_cross_syntax_macos arm64-apple-macos13)
# A list with a section for a function that has no label, and a missing section, must both fail (planted copies of the checklist).
add_test(NAME io_backend_lists_planted COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
         -DBACKEND_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cross_syntax/planted_backend.md -P ${CMAKE_CURRENT_SOURCE_DIR}/check_io_backend_lists.cmake)
set_tests_properties(io_backend_lists_planted PROPERTIES WILL_FAIL TRUE)
engine_label_tests(io_backend_lists io_backend_lists_planted io_cross_syntax_windows io_cross_syntax_macos)
