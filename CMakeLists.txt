cmake_minimum_required(VERSION 3.13.0)
project(VoxelEngine VERSION 0.1.0 LANGUAGES C CXX)

find_package(Catch2 3 REQUIRED)
find_package(quill REQUIRED)
find_library(App src)

add_subdirectory(lib/lib-readfile)
add_subdirectory(src)
add_subdirectory(tests)
add_executable(VoxelEngine)
target_sources(VoxelEngine
  PRIVATE
    main.cpp
)

target_compile_options(VoxelEngine PRIVATE -Wall -Wextra -Wfloat-equal -Wundef
                       -Wshadow -Wpointer-arith -Wcast-align
                       -Wstrict-overflow=5 -Wwrite-strings -Wcast-qual -Wswitch-default
                       -Wconversion -Wunreachable-code)

target_link_libraries(VoxelEngine
    PRIVATE
        SDL3
        App
)

include(CTest)
#include(Catch)
#catch_discover_tests(tests)
enable_testing()
