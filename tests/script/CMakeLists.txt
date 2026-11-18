# The script tests (Phase 27): the smoke test of the vendored LuaJIT (Day 445) and, from Day 446, the module. Like tests/audio, they do not apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions). One executable per sanitizer flavor; the flavors link the same `luajit` library
# (its assembler core is not instrumented, KNOWN-ISSUES.md).
if(NOT TARGET luajit)
  return()  # LuaJIT builds on Linux x86-64 only so far
endif()

set(SCRIPT_TEST_SOURCES
  ${CMAKE_SOURCE_DIR}/engine/script/luajit/luajit_smoke_test.cpp  # Day 445, moved beside the backend on Day 448 (the one place that names LuaJIT)
  value_test.cpp  # Day 446
  backend_test.cpp  # Day 446
  backend_conformance_test.cpp  # Day 447
  luajit_backend_test.cpp  # Day 448
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_script_tests target script_lib foundation_lib suffix label)
  add_executable(${target} ${SCRIPT_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${script_lib} ${foundation_lib} Catch2::Catch2WithMain Threads::Threads)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_script_tests(script_tests script foundation "" "script")
if(ENGINE_HAVE_ASAN)
  engine_add_script_tests(script_tests_asan script_asan foundation_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_script_tests(script_tests_tsan script_tsan foundation_tsan " [tsan]" "tsan")
endif()
