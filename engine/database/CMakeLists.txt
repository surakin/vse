# Phase 19: the database module. The interface and the in-memory implementation arrive on Days 252-253; today only the SQLite library
# (Day 251): sqlite/ is the one directory that includes sqlite3.h (tools/sqlite_boundary_check). Layer 10, depends on the foundation only.
# Headers are included as "database/<name>.h", rooted at engine/. Built plain, ASan and TSan from one source list; the vendored sqlite3
# library is built once (third_party/sqlite) and linked into every flavor.
set(DATABASE_SQLITE_SOURCES
  sqlite/sqlite_version.cpp  # Day 251
)
set(DATABASE_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_database_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _database_flavors "_asan")
  endif()
  foreach(suffix "" ${_database_flavors})
    add_library(database_sqlite${suffix} STATIC ${DATABASE_SQLITE_SOURCES})
    target_include_directories(database_sqlite${suffix} PUBLIC ${DATABASE_INCLUDE_ROOT})
    target_compile_options(database_sqlite${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
    target_link_libraries(database_sqlite${suffix} PUBLIC foundation_base${suffix} PRIVATE sqlite3)
  endforeach()
  return()
endif()

foreach(suffix "" "_asan" "_tsan")
  add_library(database_sqlite${suffix} STATIC ${DATABASE_SQLITE_SOURCES})
  target_include_directories(database_sqlite${suffix} PUBLIC ${DATABASE_INCLUDE_ROOT})
  target_compile_options(database_sqlite${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(database_sqlite${suffix} PUBLIC foundation${suffix} PRIVATE sqlite3)
endforeach()
