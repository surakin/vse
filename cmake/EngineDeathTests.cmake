# Death tests (Phase 7, engine/foundation/debug/death_test.h): run code that crashes in a child process.
#
# The child is a fresh copy of the test executable. It finds out it is a child from the environment, and the Catch2
# test case below (hidden: its tag starts with a dot, so only the filter `[.death-child]` runs it) hands control to the
# function the parent asked for. A test target that uses RunInChild() calls engine_enable_death_tests(<target>) once.
set(ENGINE_DEATH_TEST_CHILD_SOURCE ${CMAKE_SOURCE_DIR}/tests/debug/support/death_test_child.cpp)

function(engine_enable_death_tests target)
  target_sources(${target} PRIVATE ${ENGINE_DEATH_TEST_CHILD_SOURCE})
endfunction()
