# The database tests (Phase 19). Like tests/spatial, they do not apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions);
# the library under test is built without. One executable per sanitizer flavor.
set(DATABASE_TEST_SOURCES
  sqlite_smoke_test.cpp  # Day 251
  conformance_stub_test.cpp  # Day 252
  interface_test.cpp  # Day 252
  sqlite_memory_test.cpp  # Day 253
  sqlite_file_test.cpp  # Day 254
  sqlite_crash_test.cpp  # Day 254
  sqlite_backend_test.cpp  # Day 255
)

# ThreadSanitizer's suppressions for SQLite's lock-free WAL index header (tests/database/tsan.supp): Linux only (MSVC has no ThreadSanitizer).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND DATABASE_TEST_SOURCES tsan_options.cpp)
  set_source_files_properties(tsan_options.cpp PROPERTIES COMPILE_DEFINITIONS "ENGINE_DATABASE_TSAN_SUPP=\"${CMAKE_CURRENT_SOURCE_DIR}/tsan.supp\"")
endif()

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_database_tests target database_lib suffix label)
  add_executable(${target} ${DATABASE_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${database_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_database_tests(database_tests database_sqlite "" "database")
if(ENGINE_HAVE_ASAN)
  engine_add_database_tests(database_tests_asan database_sqlite_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_database_tests(database_tests_tsan database_sqlite_tsan " [tsan]" "tsan")
endif()
