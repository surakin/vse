# Like /tests/foundation, deliberately does NOT apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are
# exception-based. The `rhi` library under test is still built
# exception/RTTI-free.
set(RHI_TEST_SOURCES
  smoke_test.cpp
  types_test.cpp
  handle_pool_test.cpp
  null_device_test.cpp
  vulkan_loader_test.cpp
)

# Header self-containment: one generated translation unit per public RHI
# header that includes only that header, so a header that relies on another
# having been included first (or that a sibling header shadows a foundation
# one for) fails to build. Headers are globbed, so new ones are covered
# automatically.
file(GLOB RHI_PUBLIC_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/engine/rhi/*.h)
set(RHI_HEADER_CHECK_SOURCES "")
foreach(header ${RHI_PUBLIC_HEADERS})
  get_filename_component(header_name ${header} NAME_WE)
  set(check_src ${CMAKE_CURRENT_BINARY_DIR}/header_check_${header_name}.cpp)
  file(GENERATE OUTPUT ${check_src} CONTENT "#include \"rhi/${header_name}.h\"\n")
  list(APPEND RHI_HEADER_CHECK_SOURCES ${check_src})
endforeach()

find_package(Threads REQUIRED)
include(Catch)

# One executable per sanitizer flavor; ctest names get a suffix so the
# flavors don't collide. GPU tests (tag [gpu], label `gpu`) are added from
# Day 49 and skip when no Vulkan device exists.
function(engine_add_rhi_tests target rhi_lib rhi_vulkan_lib suffix label)
  add_executable(${target} ${RHI_TEST_SOURCES} ${RHI_HEADER_CHECK_SOURCES})
  target_link_libraries(${target} PRIVATE ${rhi_lib} ${rhi_vulkan_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  catch_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_rhi_tests(rhi_tests rhi rhi_vulkan "" "rhi")
engine_add_rhi_tests(rhi_tests_asan rhi_asan rhi_vulkan_asan " [asan]" "asan")
