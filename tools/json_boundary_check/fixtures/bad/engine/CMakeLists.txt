add_library(leak leak.cpp)
target_link_libraries(leak PRIVATE yyjson)
add_library(leak2 leak.cpp)
target_link_libraries(leak2 PRIVATE
  foundation
  yyjson)
