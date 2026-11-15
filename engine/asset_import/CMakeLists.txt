# Phase 22 Day 335: the glTF importer (library `asset_import`): glTF 2.0 files in memory into the engine's own resources, for a program that imports
# at run time (the host) and for `asset_mint import-gltf`. A leaf above `asset`: no runtime engine module includes it. yyjson (third_party/yyjson) is
# reached only through json_view.cpp. Headers are included as "asset_import/<name>.h", rooted at engine/. Built plain, ASan and TSan.
set(ASSET_IMPORT_SOURCES
  json_view.cpp  # Day 335
  gltf_container.cpp  # Day 335
  gltf_accessor.cpp  # Day 335
  gltf_import.cpp  # Day 335
  gltf_mesh.cpp  # Day 336
  gltf_names.cpp  # Day 337
  gltf_nodes.cpp  # Day 337
  gltf_skin.cpp  # Day 337
  gltf_clip.cpp  # Day 337
  image_decode.cpp  # Day 338
  gltf_async.cpp  # Day 338
  gltf_material.cpp  # Day 338
  material_source.cpp  # Day 338: the material minter moved here from tools/asset_mint
)
set(ASSET_IMPORT_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_import_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _import_flavors "_asan")
  endif()
  foreach(suffix "" ${_import_flavors})
    add_library(asset_import${suffix} STATIC ${ASSET_IMPORT_SOURCES})
    target_include_directories(asset_import${suffix} PUBLIC ${ASSET_IMPORT_INCLUDE_ROOT})
    target_compile_options(asset_import${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
    target_link_libraries(asset_import${suffix} PUBLIC asset${suffix} jobs${suffix} renderer_passes${suffix} foundation${suffix})
    target_link_libraries(asset_import${suffix} PRIVATE yyjson${suffix} png${suffix} stb_jpeg${suffix})
  endforeach()
  return()
endif()

foreach(suffix "" "_asan" "_tsan")
  add_library(asset_import${suffix} STATIC ${ASSET_IMPORT_SOURCES})
  target_include_directories(asset_import${suffix} PUBLIC ${ASSET_IMPORT_INCLUDE_ROOT})
  target_compile_options(asset_import${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(asset_import${suffix} PUBLIC asset${suffix} jobs${suffix} renderer_passes${suffix} foundation${suffix})
  target_link_libraries(asset_import${suffix} PRIVATE yyjson${suffix} png${suffix} stb_jpeg${suffix})
endforeach()
