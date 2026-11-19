# Phase 27 Day 475: the script demo. A crowd of bodies, each with a small Lua behavior, a player, pickups and a planner that searches a
# field with task.parallel_for, in the full frame order (scripts, physics, events) and no window: the world is in a small file set that
# the program and the tests (tests/script_demo) both build, so they run the very same game. The script is scripts/demo.lua, embedded by
# the build (demo_script.cpp.in). The windowed view with its overlay is not built yet (KNOWN-ISSUES.md).
if(NOT TARGET luajit)
  return()  # LuaJIT builds on Linux and Windows x86-64 so far (macOS: Day 474)
endif()

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/scripts/demo.lua DEMO_LUA)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scripts/demo.lua)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/demo_script.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/demo_script.cpp @ONLY)

add_executable(script_demo main.cpp demo_world.cpp ${CMAKE_CURRENT_BINARY_DIR}/demo_script.cpp)
engine_split_debug_info(script_demo)
target_compile_options(script_demo PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_include_directories(script_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(script_demo PRIVATE script_physics script physics storage ecs jobs foundation)
