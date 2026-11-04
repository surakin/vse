# Like /tests/foundation, deliberately does NOT apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are
# exception-based. The `rhi` library under test is still built
# exception/RTTI-free.
set(RHI_TEST_SOURCES
  smoke_test.cpp
)

find_package(Threads REQUIRED)
include(Catch)

# One executable per sanitizer flavor; ctest names get a suffix so the
# flavors don't collide. GPU tests (tag [gpu], label `gpu`) are added from
# Day 49 and skip when no Vulkan device exists.
function(engine_add_rhi_tests target rhi_lib rhi_vulkan_lib suffix label)
  add_executable(${target} ${RHI_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${rhi_lib} ${rhi_vulkan_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  catch_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_rhi_tests(rhi_tests rhi rhi_vulkan "" "rhi")
engine_add_rhi_tests(rhi_tests_asan rhi_asan rhi_vulkan_asan " [asan]" "asan")
