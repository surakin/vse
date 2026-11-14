# The animation tests (Phase 22 Day 321). Like tests/asset, they do not apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use
# exceptions); the library under test is built without. They compile the minters of tools/asset_mint directly to make their skeletons and
# clips. One executable per sanitizer flavor.
set(ANIMATION_TEST_SOURCES
  animation_test.cpp  # Day 321
  level_animation_test.cpp  # Day 327
  ${CMAKE_SOURCE_DIR}/tools/asset_mint/level_source.cpp  # Day 327: the level minter, to mint a level with skeletal components
  ${CMAKE_SOURCE_DIR}/tools/asset_mint/skeleton_source.cpp
  ${CMAKE_SOURCE_DIR}/tools/asset_mint/clip_source.cpp
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_animation_tests target lib suffix label)
  add_executable(${target} ${ANIMATION_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tools/asset_mint)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_animation_tests(animation_tests animation "" "animation")
if(ENGINE_HAVE_ASAN)
  engine_add_animation_tests(animation_tests_asan animation_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_animation_tests(animation_tests_tsan animation_tsan " [tsan]" "tsan")
endif()
