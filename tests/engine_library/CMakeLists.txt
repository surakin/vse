# Phase 23 (the engine as a loadable library): the tests of the shared build and the C API. Day 342 only checks that the option exists and defaults off;
# the C hosts (host_c.c and the others) arrive with Days 346 and 347.
add_test(NAME engine_shared_option COMMAND ${CMAKE_COMMAND} -DCACHE=${CMAKE_BINARY_DIR}/CMakeCache.txt -DROOT=${CMAKE_SOURCE_DIR}
         -P ${CMAKE_CURRENT_SOURCE_DIR}/check_option.cmake)
set_tests_properties(engine_shared_option PROPERTIES LABELS "engine_library")
engine_label_tests(engine_shared_option)

# Day 343: a C program per flavor links the shared library (ENGINE_BUILD_SHARED, Linux) and runs jobs inside it. The sanitized libraries load only into a
# program built with the same sanitizer (spec, Spike 3). Without the option the three tests exist as skipped stubs, so that the phase has the same flavors in
# every configuration (test_phase_labels).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(suffix "" "_asan" "_tsan")
    set(flavor_label "")
    if(suffix STREQUAL "_asan")
      set(flavor_label asan)
    elseif(suffix STREQUAL "_tsan")
      set(flavor_label tsan)
    endif()
    if(ENGINE_BUILD_SHARED AND TARGET engine${suffix})
      add_executable(shared_link_probe${suffix} shared_link_probe.c)
      target_link_libraries(shared_link_probe${suffix} PRIVATE engine${suffix})
      set_target_properties(shared_link_probe${suffix} PROPERTIES BUILD_RPATH "$<TARGET_FILE_DIR:engine${suffix}>")
      if(suffix STREQUAL "_asan")
        target_compile_options(shared_link_probe_asan PRIVATE ${ENGINE_ASAN_COMPILE_OPTIONS})
        target_link_options(shared_link_probe_asan PRIVATE ${ENGINE_ASAN_LINK_OPTIONS})
      elseif(suffix STREQUAL "_tsan")
        target_compile_options(shared_link_probe_tsan PRIVATE ${ENGINE_TSAN_COMPILE_OPTIONS})
        target_link_options(shared_link_probe_tsan PRIVATE ${ENGINE_TSAN_LINK_OPTIONS})
      endif()
      add_test(NAME engine_shared_link_probe${suffix} COMMAND shared_link_probe${suffix})
      # The exit code decides (the probe returns 1 for a wrong count): sanitizer reports and teardown crashes after the output must fail the test too.
    else()
      if(ENGINE_BUILD_SHARED)
        set(why "this compiler has no ${flavor_label} flavor, so no engine${suffix} library was built")
      else()
        set(why "ENGINE_BUILD_SHARED is OFF")
      endif()
      add_test(NAME engine_shared_link_probe${suffix} COMMAND ${CMAKE_COMMAND} -E echo "skipped: ${why}")
      set_tests_properties(engine_shared_link_probe${suffix} PROPERTIES SKIP_REGULAR_EXPRESSION "skipped: ")
    endif()
    set_tests_properties(engine_shared_link_probe${suffix} PROPERTIES LABELS "engine_library;${flavor_label}")
    engine_label_tests(engine_shared_link_probe${suffix})
  endforeach()
endif()
