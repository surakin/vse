# Phase 19: the database module: the interface (Day 252, library `database`), the in-memory implementation (Day 253) and the SQLite library
# (`database_sqlite`, Day 251, built on the interface): sqlite/ is the one directory that includes sqlite3.h (tools/sqlite_boundary_check). Layer 10, depends on the foundation only.
# Headers are included as "database/<name>.h", rooted at engine/. Built plain, ASan and TSan from one source list; the vendored sqlite3
# library is built once (third_party/sqlite) and linked into every flavor.
set(DATABASE_SOURCES
  status.cpp  # Day 252
  value.cpp  # Day 252
  database.cpp  # Day 252
)
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
    add_library(database${suffix} STATIC ${DATABASE_SOURCES})
    target_include_directories(database${suffix} PUBLIC ${DATABASE_INCLUDE_ROOT})
    target_compile_options(database${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
    target_link_libraries(database${suffix} PUBLIC foundation_base${suffix})
    add_library(database_sqlite${suffix} STATIC ${DATABASE_SQLITE_SOURCES})
    target_include_directories(database_sqlite${suffix} PUBLIC ${DATABASE_INCLUDE_ROOT})
    target_compile_options(database_sqlite${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
    target_link_libraries(database_sqlite${suffix} PUBLIC database${suffix} foundation_base${suffix} PRIVATE sqlite3)
  endforeach()
  return()
endif()

foreach(suffix "" "_asan" "_tsan")
  add_library(database${suffix} STATIC ${DATABASE_SOURCES})
  target_include_directories(database${suffix} PUBLIC ${DATABASE_INCLUDE_ROOT})
  target_compile_options(database${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(database${suffix} PUBLIC foundation${suffix})
  add_library(database_sqlite${suffix} STATIC ${DATABASE_SQLITE_SOURCES})
  target_include_directories(database_sqlite${suffix} PUBLIC ${DATABASE_INCLUDE_ROOT})
  target_compile_options(database_sqlite${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(database_sqlite${suffix} PUBLIC database${suffix} foundation${suffix} PRIVATE sqlite3)
endforeach()
