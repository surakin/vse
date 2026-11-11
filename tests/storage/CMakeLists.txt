# The storage tests (Phase 19): the component registry and the canonical encoding (Day 256-257), identities (258), the stores (259-263). Like tests/spatial,
# they do not apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions); the library under test is built without. One executable per sanitizer flavor.
set(STORAGE_TEST_SOURCES
  encoding_test.cpp  # Day 256
  registry_test.cpp  # Day 256
  upgrade_test.cpp  # Day 257
  registry_fuzz_test.cpp  # Day 257
  identity_test.cpp  # Day 258
  store_test.cpp  # Day 259
  world_roundtrip_test.cpp  # Day 260
  incremental_test.cpp  # Day 261
  sql_store_test.cpp  # Day 262
  old_schema.cpp  # Day 262
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_storage_tests target storage_lib suffix label)
  add_executable(${target} ${STORAGE_TEST_SOURCES})
  string(REPLACE storage database_sqlite _database_lib ${storage_lib})
  target_link_libraries(${target} PRIVATE ${storage_lib} ${_database_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/database)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_storage_tests(storage_tests storage "" "storage")
if(ENGINE_HAVE_ASAN)
  engine_add_storage_tests(storage_tests_asan storage_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_storage_tests(storage_tests_tsan storage_tsan " [tsan]" "tsan")
endif()
