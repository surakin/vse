# yyjson (https://github.com/ibireme/yyjson), vendored: see VENDORED.md and PLAN.md assumption #19. One static C library per flavor (`yyjson`,
# `yyjson_asan`, `yyjson_tsan`), so a sanitizer looks inside the parser of hostile files. Only engine/asset_import/json_view.cpp includes
# yyjson.h (tools/json_boundary_check). The definitions and the reasons for each are in VENDORED.md.
cmake_minimum_required(VERSION 3.20)

function(engine_add_yyjson name)
  add_library(${name} STATIC yyjson.c)
  target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(${name} PUBLIC
    YYJSON_DISABLE_WRITER=1
    YYJSON_DISABLE_INCR_READER=1
    YYJSON_DISABLE_FILE=1
    YYJSON_DISABLE_UTILS=1
    YYJSON_DISABLE_NON_STANDARD=1
    YYJSON_READER_DEPTH_LIMIT=128)
  set_target_properties(${name} PROPERTIES UNITY_BUILD OFF POSITION_INDEPENDENT_CODE ON)
  # Third-party C: its warnings are not ours to fix. Warnings off for this target only.
  if(MSVC AND NOT ENGINE_CLANG_CL)
    target_compile_options(${name} PRIVATE /w)
  else()
    target_compile_options(${name} PRIVATE -w)
  endif()
endfunction()

engine_add_yyjson(yyjson)
if(ENGINE_HAVE_ASAN)
  engine_add_yyjson(yyjson_asan)
  target_compile_options(yyjson_asan PRIVATE ${ENGINE_ASAN_COMPILE_OPTIONS})
endif()
if(ENGINE_HAVE_TSAN)
  engine_add_yyjson(yyjson_tsan)
  target_compile_options(yyjson_tsan PRIVATE ${ENGINE_TSAN_COMPILE_OPTIONS})
endif()
