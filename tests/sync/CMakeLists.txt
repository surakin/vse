# Phase 16 Day 209: the sync module (Atomic, Mutex, ConditionVariable, Thread). Like tests/types, does not apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2 needs exceptions). Oracle files include the standard headers on purpose.
set(SYNC_TEST_SOURCES
  sync_test.cpp
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_sync_tests target foundation_lib suffix label)
  add_executable(${target} ${SYNC_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${foundation_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_sync_tests(sync_tests foundation_base "" "sync")
if(ENGINE_HAVE_ASAN)
  engine_add_sync_tests(sync_tests_asan foundation_base_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)
  engine_add_sync_tests(sync_tests_tsan foundation_base_tsan " [tsan]" "tsan")
endif()
