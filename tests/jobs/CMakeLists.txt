# Like /tests/foundation, deliberately does NOT apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are
# exception-based. The `jobs` library under test is still built
# exception/RTTI-free.
set(JOBS_TEST_SOURCES
  smoke_test.cpp
  frame_pool_test.cpp
  job_test.cpp
  stress_test.cpp
  async_io_test.cpp
  parallel_for_test.cpp
  counter_test.cpp
  scheduler_test.cpp
  deque_test.cpp
  worker_pool_test.cpp
  wake_latency_test.cpp
)
# macOS (Phase 26 Day 305): the platform half's own cases (names set from inside, no pinning, the CPU list).
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  list(APPEND JOBS_TEST_SOURCES macos/thread_platform_macos_test.cpp)
endif()

find_package(Threads REQUIRED)
include(Catch)

# Windows (Phase 13 Day 153): plain and ASan; no ThreadSanitizer, so neither the tsan flavor nor its canary.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_jobs_test_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _jobs_test_flavors "_asan")
  endif()
  foreach(suffix "" ${_jobs_test_flavors})
    add_executable(jobs_tests${suffix} ${JOBS_TEST_SOURCES} topology_windows_test.cpp)
    target_link_libraries(jobs_tests${suffix} PRIVATE jobs${suffix} Catch2::Catch2WithMain)
    target_include_directories(jobs_tests${suffix} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/support ${CMAKE_SOURCE_DIR}/tests/io)
    engine_asan_runtime(jobs_tests${suffix})
  endforeach()
  engine_discover_tests(jobs_tests TEST_SUFFIX "" PROPERTIES LABELS "jobs")
  if(ENGINE_HAVE_ASAN)
    engine_discover_tests(jobs_tests_asan TEST_SUFFIX " [asan]" PROPERTIES LABELS "asan")
  endif()
  return()
endif()

# One executable per sanitizer flavor. ctest names get a suffix so the
# three flavors don't collide, and the TSan ones carry the `tsan` label
# (`ctest -L tsan` runs just those).
function(engine_add_jobs_tests target jobs_lib suffix label)
  add_executable(${target} ${JOBS_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${jobs_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/support ${CMAKE_SOURCE_DIR}/tests/io)
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_jobs_tests(jobs_tests jobs "" "jobs")
engine_add_jobs_tests(jobs_tests_asan jobs_asan " [asan]" "asan")
engine_add_jobs_tests(jobs_tests_tsan jobs_tsan " [tsan]" "tsan")

# TSan canary: a program with a deliberate data race, built under TSan.
# The test passes only if the output contains TSan's data-race report. It
# must NOT be a bare WILL_FAIL: that would also "pass" when the binary
# merely crashes or TSan fails to start (e.g. an unsupported ASLR
# configuration), which would make every later "TSan clean" result
# meaningless. If TSan stops detecting races the report disappears and
# ctest goes red.
add_executable(tsan_canary tsan_canary.cpp)
target_compile_options(tsan_canary PRIVATE -fsanitize=thread -g)
target_link_options(tsan_canary PRIVATE -fsanitize=thread)
target_link_libraries(tsan_canary PRIVATE Threads::Threads)
target_include_directories(tsan_canary PRIVATE ${CMAKE_SOURCE_DIR}/engine/foundation)  # sync/thread.h (Phase 16): header-only, so the canary still needs no library
add_test(NAME tsan_canary_detects_race COMMAND tsan_canary)
set_tests_properties(tsan_canary_detects_race PROPERTIES
  PASS_REGULAR_EXPRESSION "ThreadSanitizer: data race"
  LABELS "tsan")
# On macOS the sanitizers' abort_on_error defaults to 1 (Linux: 0), so the report ends in abort() and ctest counts the abort as a failure
# whatever the output says (Phase 26 Day 305: exit 134 against 66). The canary asks for the Linux behavior there; its pass condition is unchanged.
if(APPLE)
  set_tests_properties(tsan_canary_detects_race PROPERTIES ENVIRONMENT "TSAN_OPTIONS=abort_on_error=0")
endif()
engine_label_tests(tsan_canary_detects_race)
