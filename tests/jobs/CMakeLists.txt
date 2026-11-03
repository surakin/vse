# Like /tests/foundation, deliberately does NOT apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are
# exception-based. The `jobs` library under test is still built
# exception/RTTI-free.
set(JOBS_TEST_SOURCES
  smoke_test.cpp
  frame_pool_test.cpp
)

find_package(Threads REQUIRED)
include(Catch)

# One executable per sanitizer flavor. ctest names get a suffix so the
# three flavors don't collide, and the TSan ones carry the `tsan` label
# (`ctest -L tsan` runs just those).
function(engine_add_jobs_tests target jobs_lib suffix label)
  add_executable(${target} ${JOBS_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${jobs_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  catch_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_jobs_tests(jobs_tests jobs "" "jobs")
engine_add_jobs_tests(jobs_tests_asan jobs_asan " [asan]" "asan")
engine_add_jobs_tests(jobs_tests_tsan jobs_tsan " [tsan]" "tsan")

# TSan canary: a program with a deliberate data race, built under TSan,
# registered as an expected FAILURE. If TSan stops detecting races (broken
# toolchain/kernel interaction, flags dropped from the target), this test
# starts passing and ctest reports it red — so "TSan is clean" results
# elsewhere can be trusted.
add_executable(tsan_canary tsan_canary.cpp)
target_compile_options(tsan_canary PRIVATE -fsanitize=thread -g)
target_link_options(tsan_canary PRIVATE -fsanitize=thread)
target_link_libraries(tsan_canary PRIVATE Threads::Threads)
add_test(NAME tsan_canary_detects_race COMMAND tsan_canary)
set_tests_properties(tsan_canary_detects_race PROPERTIES WILL_FAIL TRUE LABELS "tsan")
