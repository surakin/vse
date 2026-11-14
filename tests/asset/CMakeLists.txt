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
  bc_test.cpp  # Day 324
  ${CMAKE_SOURCE_DIR}/tools/asset_mint/texture_source.cpp  # Day 323: the texture minter under test
  material_test.cpp  # Day 326
  ${CMAKE_SOURCE_DIR}/tools/asset_mint/material_source.cpp  # Day 326: the material minter under test
  level_test.cpp  # Day 327
  manager_test.cpp  # Day 328
  gpu_loader_test.cpp  # Day 329
  async_loader_test.cpp  # Day 330
  resource_server_test.cpp  # Day 331
  network_backend_test.cpp  # Day 332
  mint_determinism_test.cpp  # Day 333
  ${CMAKE_SOURCE_DIR}/tools/asset_mint/project.cpp  # Day 333: the project side of the minter under test
  ${CMAKE_SOURCE_DIR}/tools/asset_mint/level_source.cpp  # Day 327: the level minter under test
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_asset_tests target asset_lib suffix label)
  add_executable(${target} ${ASSET_TEST_SOURCES})
  string(REPLACE "asset" "" renderer_suffix ${asset_lib})  # Day 326: the material minter reads the passes' template tables
  string(REPLACE "asset" "renderer" renderer_lib ${asset_lib})  # Day 323: the texture test uploads a blob to a null device
  string(REPLACE "asset" "animation" animation_lib ${asset_lib})  # Day 333: the project side of the minter registers the animation components
  target_link_libraries(${target} PRIVATE ${asset_lib} ${animation_lib} renderer_passes${renderer_suffix} ${renderer_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tools/asset_mint)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

# Day 332: the network backend over real loopback sockets, labelled `network`.
set(ASSET_SOCKET_TEST_SOURCES network_socket_test.cpp support.h backend_conformance.h)
function(engine_add_asset_socket_tests target asset_lib suffix label)
  add_executable(${target} ${ASSET_SOCKET_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${asset_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label};network")
endfunction()

engine_add_asset_tests(asset_tests asset "" "asset")
engine_add_asset_socket_tests(asset_tests_sockets asset "" "asset")
if(ENGINE_HAVE_ASAN)
  engine_add_asset_tests(asset_tests_asan asset_asan " [asan]" "asan")
  engine_add_asset_socket_tests(asset_tests_asan_sockets asset_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_asset_tests(asset_tests_tsan asset_tsan " [tsan]" "tsan")
  engine_add_asset_socket_tests(asset_tests_tsan_sockets asset_tsan " [tsan]" "tsan")
endif()
