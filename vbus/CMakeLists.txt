
cmake_minimum_required(VERSION 3.20)
project(vbus LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /Zc:preprocessor)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(vbus_core
  src/core/clock.h
  src/core/scheduler.h
  src/core/scheduler.cpp
  src/bus/frame.h
  src/bus/ibus.h
  src/bus/eth_bus.h src/bus/eth_bus.cpp
  src/bus/can_bus.h src/bus/can_bus.cpp
  src/taps/recorder.h src/taps/recorder.cpp
  src/taps/stats.h
  src/net/capture_base.h
  src/net/udp_endpoint.h  src/net/udp_endpoint.cpp
  src/net/tcp_proxy.h     src/net/tcp_proxy.cpp
 "src/core/core.h")

target_include_directories(vbus_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(vbusd src/daemon/vbusd.cpp "src/core/core.h")
target_link_libraries(vbusd PRIVATE vbus_core)

add_executable(vbusctl src/cli/vbusctl.cpp "src/core/core.h")
target_link_libraries(vbusctl PRIVATE vbus_core)

add_executable(vbus_bench src/cli/vbus_bench.cpp)
target_include_directories(vbus_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(vbus_client SHARED
  src/client/vbus_client.h
  src/client/vbus_client.cpp
 "src/core/core.h")
target_link_libraries(vbus_client PRIVATE vbus_core)

# Windows-specific libraries
if(WIN32)
  target_link_libraries(vbusd PRIVATE ws2_32)
  target_link_libraries(vbusctl PRIVATE ws2_32)
  target_link_libraries(vbus_bench PRIVATE ws2_32)
  target_link_libraries(vbus_client PRIVATE ws2_32)
endif()
