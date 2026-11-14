# The asset pipeline tests (Phase 22). Like tests/net, they do not apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions);
# the library under test is built without. One executable per sanitizer flavor.
set(ASSET_TEST_SOURCES
  module_test.cpp  # Day 313
  hash_test.cpp    # Day 314
  id_test.cpp      # Day 314
  blob_test.cpp    # Day 314
  table_test.cpp   # Day 314
  support.h        # Day 314
  backend_conformance.h  # Day 315
  filesystem_backend_test.cpp  # Day 315
  memory_backend_test.cpp  # Day 315
  mount_table_test.cpp  # Day 315
  load_buffer_pool_test.cpp  # Day 315
  async_table_test.cpp  # Day 315
  memory_file.h  # Day 316
  pak_test.cpp  # Day 316
  skeleton_test.cpp  # Day 318
  mesh_test.cpp  # Day 319
  clip_test.cpp  # Day 320
  ${CMAKE_SOURCE_DIR}/tools/asset_mint/skeleton_source.cpp  # Day 318: the minter under test
  ${CMAKE_SOURCE_DIR}/tools/asset_mint/mesh_source.cpp  # Day 319: the mesh minter under test
  ${CMAKE_SOURCE_DIR}/tools/asset_mint/clip_source.cpp  # Day 320: the clip minter under test
  texture_test.cpp  # Day 323
  ${CMAKE_SOURCE_DIR}/tools/asset_mint/texture_source.cpp  # Day 323: the texture minter under test
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_asset_tests target asset_lib suffix label)
  add_executable(${target} ${ASSET_TEST_SOURCES})
  string(REPLACE "asset" "renderer" renderer_lib ${asset_lib})  # Day 323: the texture test uploads a blob to a null device
  target_link_libraries(${target} PRIVATE ${asset_lib} ${renderer_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tools/asset_mint)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_asset_tests(asset_tests asset "" "asset")
if(ENGINE_HAVE_ASAN)
  engine_add_asset_tests(asset_tests_asan asset_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_asset_tests(asset_tests_tsan asset_tsan " [tsan]" "tsan")
endif()
