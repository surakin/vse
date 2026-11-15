# Phase 23 (the engine as a loadable library): the tests of the shared build and the C API. Day 342 only checks that the option exists and defaults off;
# the C hosts (host_c.c and the others) arrive with Days 346 and 347.
add_test(NAME engine_shared_option COMMAND ${CMAKE_COMMAND} -DCACHE=${CMAKE_BINARY_DIR}/CMakeCache.txt -DROOT=${CMAKE_SOURCE_DIR}
         -P ${CMAKE_CURRENT_SOURCE_DIR}/check_option.cmake)
set_tests_properties(engine_shared_option PROPERTIES LABELS "engine_library")
engine_label_tests(engine_shared_option)
