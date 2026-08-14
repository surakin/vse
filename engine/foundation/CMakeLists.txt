# Per-platform implementation of os.h — only linux/ exists so far
# (PLAN.md assumption #11: implementation stays Linux-only until its
# phase; this selection is where a later windows/os.cpp or macos/os.cpp
# plugs in additively, no other CMakeLists change needed).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(FOUNDATION_PLATFORM_SOURCES linux/os.cpp)
else()
  message(FATAL_ERROR "No foundation os.h implementation for this platform yet")
endif()

set(FOUNDATION_SOURCES
  foundation.cpp
  mem.cpp
  arena_allocator.cpp
  pool_allocator.cpp
  stack_allocator.cpp
  log.cpp
  utf8_codec.cpp
  grapheme.cpp
  generated/grapheme_break_table.cpp
  ${FOUNDATION_PLATFORM_SOURCES}
)

add_library(foundation STATIC ${FOUNDATION_SOURCES})
target_include_directories(foundation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(engine_main main.cpp)
target_link_libraries(engine_main PRIVATE foundation)

# Sanitized variants: separate targets built from the same sources, not
# a build-wide flag, so both the normal and sanitized binaries exist
# side by side from one configure — `cmake --build build --target
# engine_main_asan && ./build/engine/foundation/engine_main_asan`.
# PUBLIC on foundation_asan's options propagates to engine_main_asan's
# own compile/link, so they don't need repeating there.
add_library(foundation_asan STATIC ${FOUNDATION_SOURCES})
target_include_directories(foundation_asan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(foundation_asan PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -g)
target_link_options(foundation_asan PUBLIC -fsanitize=address,undefined)

add_executable(engine_main_asan main.cpp)
target_link_libraries(engine_main_asan PRIVATE foundation_asan)
