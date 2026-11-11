# The networking tests (Phase 20). Like tests/database, they do not apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions);
# the library under test is built without. One executable per sanitizer flavor. Real-socket tests (from Day 268) are tagged [network].
set(NET_TEST_SOURCES
  address_test.cpp  # Day 267
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_net_tests target net_lib suffix label)
  add_executable(${target} ${NET_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${net_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_net_tests(net_tests net "" "net")
if(ENGINE_HAVE_ASAN)
  engine_add_net_tests(net_tests_asan net_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_net_tests(net_tests_tsan net_tsan " [tsan]" "tsan")
endif()
