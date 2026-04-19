add_library(Graphics)
target_sources(Graphics
    PRIVATE
        vulkan_renderer_init.cpp
        vulkan_renderer_swapchain.cpp
        vulkan_renderer.cpp

    PUBLIC
        FILE_SET HEADERS
        FILES
            vulkan_renderer.hpp
)

target_compile_options(Graphics
    PRIVATE
        -Wall -Wextra -Wfloat-equal -Wundef
        -Wshadow -Wpointer-arith -Wcast-align
        -Wstrict-overflow=5 -Wwrite-strings -Wcast-qual -Wswitch-default
        -Wconversion -Wunreachable-code
        
    PUBLIC
        -std=c++20)

find_package(quill REQUIRED)
target_link_libraries(Graphics
    PUBLIC
        ReadFile
        SDL3
        quill::quill
        vulkan
)
