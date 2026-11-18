# Phase 27 Day 466: the physics bindings of the script module. Layer 14, above script and physics (docs/superpowers/specs/2026-11-18-scripting-design.md):
# the one target that names both, so `script` never includes `physics`. Headers are included as "script_physics/<name>.h", rooted at engine/. Linux only
# until the Windows and macOS days (473 and 474), like script. One library per sanitizer flavor.
set(SCRIPT_PHYSICS_SOURCES
  physics_layers.cpp  # Day 466
  physics_components.cpp  # Day 466
  physics_bindings.cpp  # Day 466
)
set(SCRIPT_PHYSICS_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

foreach(suffix "" "_asan" "_tsan")
  add_library(script_physics${suffix} STATIC ${SCRIPT_PHYSICS_SOURCES})
  target_include_directories(script_physics${suffix} PUBLIC ${SCRIPT_PHYSICS_INCLUDE_ROOT})
  target_compile_options(script_physics${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(script_physics${suffix} PUBLIC script${suffix} physics${suffix})
endforeach()
