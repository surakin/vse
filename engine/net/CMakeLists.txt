# Phase 20: the networking module (library `net`). Day 267: addresses, endpoints and the error type; later days add the socket backends, the reactor
# and the transports. Layer 11, above the foundation (and, from Day 269, jobs). Headers are included as "net/<name>.h", rooted at engine/.
# Built plain, ASan and TSan from one source list.
set(NET_SOURCES
  status.cpp  # Day 267
  address.cpp  # Day 267
  endpoint.cpp  # Day 267
  frame.cpp  # Day 271
  codec.cpp  # Day 272
  transport.cpp  # Day 273
  sim_network.cpp  # Day 273
  sim_transport.cpp  # Day 273
  sim_datagram.cpp  # Day 274
  udp_protocol.cpp  # Day 274
  udp_transport.cpp  # Day 274
  sim_stream.cpp  # Day 277
  rate_limit.cpp  # Day 279
  tcp_transport.cpp  # Day 277
)
# The socket backend (Day 268): the shared POSIX half and the operating system's hooks; Winsock on Windows (Day 278). The files
# above it that need real sockets (the datagram port, the socket stream provider) are built wherever a backend is.
set(NET_SOCKET_SOURCES
  udp_port.cpp  # Day 274
  socket_stream.cpp  # Day 277
)
set(NET_POSIX_SOURCES
  posix/posix_error.cpp  # Day 268
  posix/socket_posix.cpp  # Day 268
  posix/resolve_posix.cpp  # Day 270
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND NET_POSIX_SOURCES linux/linux_hooks.cpp)  # Day 268
endif()
set(NET_WINDOWS_SOURCES
  windows/wsa_error.cpp  # Day 278
  windows/socket_windows.cpp  # Day 278
  windows/resolve_windows.cpp  # Day 278
  windows/poller_select.cpp  # Day 278
)
# The reactor (Day 269): the readiness core is neutral; the poller is the platform's (epoll on Linux, select on Windows).
set(NET_REACTOR_SOURCES
  reactor.cpp  # Day 269
  async.cpp  # Day 269
  resolver.cpp  # Day 270
  resolve.cpp  # Day 270
  listener.cpp  # Day 270
  tcp_stream.cpp  # Day 271
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND NET_REACTOR_SOURCES linux/poller_epoll.cpp)  # Day 269
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  list(APPEND NET_SOURCES ${NET_WINDOWS_SOURCES} ${NET_SOCKET_SOURCES} ${NET_REACTOR_SOURCES})
else()
  list(APPEND NET_SOURCES ${NET_POSIX_SOURCES} ${NET_SOCKET_SOURCES} ${NET_REACTOR_SOURCES})
endif()
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
    # ws2_32 (Winsock 2) and iphlpapi (if_nametoindex, for scope names) ship with Windows.
    target_link_libraries(net${suffix} PUBLIC jobs${suffix} foundation${suffix} ws2_32 iphlpapi)
  endforeach()
  return()
endif()

foreach(suffix "" "_asan" "_tsan")
  add_library(net${suffix} STATIC ${NET_SOURCES})
  target_include_directories(net${suffix} PUBLIC ${NET_INCLUDE_ROOT})
  target_compile_options(net${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(net${suffix} PUBLIC jobs${suffix} foundation${suffix})
endforeach()
