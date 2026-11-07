# A scratch project for windows_toolchain_discovery_* (tests/windows/discovery/check.cmake): built with a toolchain file from an
# environment with no Visual Studio variables, it must compile, link and run a program that includes <windows.h>.
cmake_minimum_required(VERSION 3.21)
project(engine_toolchain_discovery CXX)
add_executable(hello hello.cpp)
