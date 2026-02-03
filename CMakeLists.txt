cmake_minimum_required(VERSION 4.1)
project(MinimalVst3HostForWindows)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}")

include_directories(./third_party/vst3sdk)

add_executable(MinimalVst3HostForWindows
        src/MinimalVst3HostForWindows.cpp)

target_link_libraries(MinimalVst3HostForWindows PRIVATE avrt)
