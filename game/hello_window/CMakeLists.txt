# The Phase 1 hello-window program (it lived in engine/foundation as engine_main until Phase 12 Day 143): opens a window on the chosen backend and
# logs the input state. `cmake --build build/linux --target hello_window_asan && build/linux/game/hello_window/hello_window_asan` runs it under ASan/UBSan.
add_executable(hello_window main.cpp)
target_compile_options(hello_window PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(hello_window PRIVATE platform)

if(TARGET platform_asan) # (not with clang-cl on Windows, whose configuration has no ASan flavor)
  add_executable(hello_window_asan main.cpp)
  target_compile_options(hello_window_asan PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(hello_window_asan PRIVATE platform_asan)
  engine_asan_runtime(hello_window_asan)
endif()
