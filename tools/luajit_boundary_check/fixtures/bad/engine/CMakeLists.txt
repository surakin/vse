add_library(leak leak.cpp)
target_link_libraries(leak PRIVATE luajit)
add_library(leak2 leak.cpp)
target_link_libraries(leak2 PRIVATE
  foundation
  luajit)
