# Phase 22: the asset pipeline and resource system (library `asset`). Day 313: the skeleton (one translation unit naming the module); the
# following days add resource identity, the blob container, the backends and the resource manager. Layer 13, above the renderer and below
# only programs. Headers are included as "asset/<name>.h", rooted at engine/. Built plain, ASan and TSan from one source list.
set(ASSET_SOURCES
  asset_module.cpp  # Day 313
  status.cpp  # Day 314
  resource_type.cpp  # Day 314
  resource_id.cpp  # Day 314
  content_hash.cpp  # Day 314
  blob.cpp  # Day 314
  resource_table.cpp  # Day 314
  backend.cpp  # Day 315
  load_buffer_pool.cpp  # Day 315
  asset_settings.cpp  # Day 315
  filesystem_backend.cpp  # Day 315
  memory_backend.cpp  # Day 315
  faulty_backend.cpp  # Day 315
  mount_table.cpp  # Day 315
  pak.cpp  # Day 316
  pak_backend.cpp  # Day 316
  skeleton.cpp  # Day 318
  hull.cpp  # Day 393
  mesh_collider.cpp  # Day 394
  script_blob.cpp  # Phase 27 Day 462
  audio_clip.cpp  # Phase 26 Day 418
  mesh.cpp  # Day 319
  clip.cpp  # Day 320
  texture_mips.cpp  # Day 323
  texture.cpp  # Day 323
  block_compress.cpp  # Day 324
  texture_mint.cpp  # Day 338
  cube_map.cpp  # Phase 28 Day 480
  height_map.cpp  # Phase 28 Day 486
  material.cpp  # Day 326
  level.cpp  # Day 327
  resource_manager.cpp  # Day 328
  cpu_loaders.cpp  # Day 328
  gpu_loaders.cpp  # Day 329
  async_loader.cpp  # Day 330
  resource_protocol.cpp  # Day 331
  resource_server.cpp  # Day 331
  blob_cache.cpp  # Day 332
  network_backend.cpp  # Day 332
)
set(ASSET_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_asset_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _asset_flavors "_asan")
  endif()
  foreach(suffix "" ${_asset_flavors})
    add_library(asset${suffix} STATIC ${ASSET_SOURCES})
    target_include_directories(asset${suffix} PUBLIC ${ASSET_INCLUDE_ROOT})
    target_compile_options(asset${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
    target_link_libraries(asset${suffix} PUBLIC renderer_passes${suffix} renderer${suffix} storage${suffix} ecs${suffix} net${suffix} jobs${suffix} foundation${suffix})  # storage and ecs: the level format, Day 327
  endforeach()
  return()
endif()

foreach(suffix "" "_asan" "_tsan")
  add_library(asset${suffix} STATIC ${ASSET_SOURCES})
  target_include_directories(asset${suffix} PUBLIC ${ASSET_INCLUDE_ROOT})
  target_compile_options(asset${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(asset${suffix} PUBLIC renderer_passes${suffix} renderer${suffix} storage${suffix} ecs${suffix} net${suffix} jobs${suffix} foundation${suffix})  # storage and ecs: the level format, Day 327
endforeach()
