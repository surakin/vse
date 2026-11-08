# Phase 14: the text layer (CPU side): font faces, glyph rasterization and atlas packing, layout, cursor and hit-testing. It depends on the
# foundation and on FreeType (third_party/freetype), which only font_freetype.cpp includes. Built plain, ASan and TSan from one source list,
# each linked against the matching foundation variant, like jobs. Headers are included as "text/<name>.h", rooted at engine/.
set(TEXT_SOURCES
  font_freetype.cpp
  shelf_packer.cpp
  glyph_atlas.cpp
  layout.cpp
  cursor.cpp
)
set(TEXT_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_text_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _text_flavors "_asan")
  endif()
  foreach(suffix "" ${_text_flavors})
    add_library(text${suffix} STATIC ${TEXT_SOURCES})
    target_include_directories(text${suffix} PUBLIC ${TEXT_INCLUDE_ROOT})
    target_compile_options(text${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
    target_link_libraries(text${suffix} PUBLIC foundation_io${suffix} PRIVATE freetype)
  endforeach()
  return()
endif()

add_library(text STATIC ${TEXT_SOURCES})
target_include_directories(text PUBLIC ${TEXT_INCLUDE_ROOT})
target_compile_options(text PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(text PUBLIC foundation PRIVATE freetype)

add_library(text_asan STATIC ${TEXT_SOURCES})
target_include_directories(text_asan PUBLIC ${TEXT_INCLUDE_ROOT})
target_compile_options(text_asan PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(text_asan PUBLIC foundation_asan PRIVATE freetype)

add_library(text_tsan STATIC ${TEXT_SOURCES})
target_include_directories(text_tsan PUBLIC ${TEXT_INCLUDE_ROOT})
target_compile_options(text_tsan PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(text_tsan PUBLIC foundation_tsan PRIVATE freetype)
