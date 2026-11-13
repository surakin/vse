# metal_mint (Phase 21 Day 299): the Metal Shading Language that slangc writes, compiled by the driver's own compiler and
# serialized into the AIR-only .metallib that shaders/<name>.metallib.inc embeds. macOS only (it needs a Metal device),
# dev-time only, never linked into the engine (PLAN.md assumption #18). The route is the interim, undocumented one the user
# chose on Day 282 (pending until a new Mac; KNOWN-ISSUES.md), isolated in macos/metal_mint.mm.
if(NOT APPLE)
  return()
endif()
add_executable(metal_mint macos/metal_mint.mm)
target_link_libraries(metal_mint PRIVATE foundation "-framework Foundation" "-framework Metal")
