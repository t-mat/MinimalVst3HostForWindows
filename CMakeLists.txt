cmake_minimum_required(VERSION 3.20)
project(MinimalVst3HostForWindows)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(MinimalVst3HostForWindows
        src/MinimalVst3HostForWindows.cpp)

target_include_directories(MinimalVst3HostForWindows PRIVATE ./third_party/vst3sdk)
target_compile_options(MinimalVst3HostForWindows PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)
target_link_libraries(MinimalVst3HostForWindows PRIVATE avrt)
