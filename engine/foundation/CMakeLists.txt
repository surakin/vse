add_library(foundation STATIC
  foundation.cpp
  os.cpp
  mem.cpp
  arena_allocator.cpp
)
target_include_directories(foundation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(engine_main main.cpp)
target_link_libraries(engine_main PRIVATE foundation)
