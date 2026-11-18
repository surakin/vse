# Phase 27 Day 446: the script module, layer 13 (docs/superpowers/specs/2026-11-18-scripting-design.md). The language-neutral backend interface and the
# value type; LuaJIT's backend (engine/script/luajit/, Day 448) and the tiny test backend (Day 447) implement it. Headers are included as "script/<name>.h",
# rooted at engine/. Linux only until the Windows and macOS days (473 and 474). One library per sanitizer flavor.
set(SCRIPT_SOURCES
  value.cpp  # Day 446
  state.cpp  # Day 446
  backend_registry.cpp  # Day 447
  script_settings.cpp  # Day 447
  expr/expr_backend.cpp  # Day 447
  bindings.cpp  # Day 450
  script_world.cpp  # Day 451
  command_buffer.cpp  # Day 452
  merge.cpp  # Day 452
  partition_context.cpp  # Day 452
  standard_bindings.cpp  # Day 450
  runtime_bindings.cpp  # Day 453
  log_buffer.cpp  # Day 453
)
if(TARGET luajit)  # the LuaJIT backend (Day 448); LuaJIT builds on Linux x86-64 only so far
  list(APPEND SCRIPT_SOURCES luajit/luajit_backend.cpp luajit/lua_patterns.cpp)
endif()
set(SCRIPT_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

foreach(suffix "" "_asan" "_tsan")
  add_library(script${suffix} STATIC ${SCRIPT_SOURCES})
  target_include_directories(script${suffix} PUBLIC ${SCRIPT_INCLUDE_ROOT})
  target_compile_options(script${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(script${suffix} PUBLIC ecs${suffix} foundation${suffix})
  if(TARGET luajit)
    target_link_libraries(script${suffix} PUBLIC luajit)
  endif()
endforeach()
