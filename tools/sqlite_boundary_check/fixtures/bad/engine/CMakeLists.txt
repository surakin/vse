add_library(leak leak.cpp)
target_link_libraries(leak PRIVATE sqlite3)
