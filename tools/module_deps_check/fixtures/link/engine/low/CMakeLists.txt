add_library(low STATIC a.cpp)
target_link_libraries(low PUBLIC mid)
