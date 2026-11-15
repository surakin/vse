# A program outside the engine's tree, built against the installed engine only: find_package(Engine) and the public headers (Phase 23, the install test).
cmake_minimum_required(VERSION 3.25)
project(engine_consumer C)
find_package(Engine REQUIRED)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE engine::engine)
