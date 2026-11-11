# Phase 20: the networking module (library `net`). Day 267: addresses, endpoints and the error type; later days add the socket backends, the reactor
# and the transports. Layer 11, above the foundation (and, from Day 269, jobs). Headers are included as "net/<name>.h", rooted at engine/.
# Built plain, ASan and TSan from one source list.
set(NET_SOURCES
  status.cpp  # Day 267
  address.cpp  # Day 267
  endpoint.cpp  # Day 267
)
set(NET_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_net_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _net_flavors "_asan")
  endif()
  foreach(suffix "" ${_net_flavors})
    add_library(net${suffix} STATIC ${NET_SOURCES})
    target_include_directories(net${suffix} PUBLIC ${NET_INCLUDE_ROOT})
    target_compile_options(net${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
    target_link_libraries(net${suffix} PUBLIC foundation_base${suffix})
  endforeach()
  return()
endif()

foreach(suffix "" "_asan" "_tsan")
  add_library(net${suffix} STATIC ${NET_SOURCES})
  target_include_directories(net${suffix} PUBLIC ${NET_INCLUDE_ROOT})
  target_compile_options(net${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(net${suffix} PUBLIC foundation${suffix})
endforeach()
