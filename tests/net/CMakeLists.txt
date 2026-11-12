# The networking tests (Phase 20). Like tests/database, they do not apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions);
# the library under test is built without. One executable per sanitizer flavor. Real-socket tests (from Day 268) are tagged [network].
set(NET_TEST_SOURCES
  address_test.cpp  # Day 267
  frame_test.cpp  # Day 271
  net_codec_test.cpp  # Day 272
)
# The socket backend's tests (Day 268); the Windows backend arrives on Day 278. Real sockets: the executable is labelled `network`.
set(NET_SOCKET_TEST_SOURCES "")
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
  list(APPEND NET_TEST_SOURCES errno_map_posix_test.cpp)  # Day 268
  list(APPEND NET_SOCKET_TEST_SOURCES socket_test.cpp socket_flags_posix_test.cpp reactor_test.cpp resolve_test.cpp listener_test.cpp tcp_test.cpp)  # Days 268-271
endif()

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
  if(NET_SOCKET_TEST_SOURCES)
    add_executable(${target}_sockets ${NET_SOCKET_TEST_SOURCES})
    target_link_libraries(${target}_sockets PRIVATE ${net_lib} Catch2::Catch2WithMain Threads::Threads)
    if(label STREQUAL "asan")
      engine_asan_runtime(${target}_sockets)
    endif()
    engine_discover_tests(${target}_sockets TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label};network")
  endif()
endfunction()

engine_add_net_tests(net_tests net "" "net")
if(ENGINE_HAVE_ASAN)
  engine_add_net_tests(net_tests_asan net_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_net_tests(net_tests_tsan net_tsan " [tsan]" "tsan")
endif()

# The backend checklist gate (a CMake script, run on every platform) and its self-test.
add_test(NAME net_backend_lists COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_net_backend_lists.cmake)
add_test(NAME net_backend_lists_planted COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
         -DBACKEND_FILE=${CMAKE_CURRENT_SOURCE_DIR}/planted_backend.md -P ${CMAKE_CURRENT_SOURCE_DIR}/check_net_backend_lists.cmake)
set_tests_properties(net_backend_lists_planted PROPERTIES WILL_FAIL TRUE)
engine_label_tests(net_backend_lists net_backend_lists_planted)
