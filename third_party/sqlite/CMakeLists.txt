# SQLite (https://sqlite.org), vendored as the amalgamation: see VENDORED.md and PLAN.md assumption #25. One static library, `sqlite3`,
# built once with plain flags and linked into the plain, ASan and TSan flavors alike. Only engine/database/sqlite/ includes sqlite3.h
# (tools/sqlite_boundary_check). The definitions and the reasons for each are in VENDORED.md.
cmake_minimum_required(VERSION 3.20)

add_library(sqlite3 STATIC sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(sqlite3 PUBLIC
  SQLITE_OS_OTHER=1
  SQLITE_THREADSAFE=2
  SQLITE_OMIT_DESERIALIZE
  SQLITE_OMIT_LOAD_EXTENSION
  SQLITE_OMIT_DEPRECATED
  SQLITE_OMIT_SHARED_CACHE
  SQLITE_DQS=0
  SQLITE_DEFAULT_FOREIGN_KEYS=1
  SQLITE_DEFAULT_MEMSTATUS=0
  SQLITE_TEMP_STORE=3)
set_target_properties(sqlite3 PROPERTIES UNITY_BUILD OFF POSITION_INDEPENDENT_CODE ON)
# The amalgamation is third-party C: its warnings are not ours to fix. Warnings off for this target only.
if(MSVC AND NOT ENGINE_CLANG_CL)
  target_compile_options(sqlite3 PRIVATE /w)
else()
  target_compile_options(sqlite3 PRIVATE -w)
endif()
