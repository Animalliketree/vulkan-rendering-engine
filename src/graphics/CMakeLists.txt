find_package(quill REQUIRED)
find_package(volk REQUIRED)

add_library(Graphics)
target_sources(Graphics
    PRIVATE
        vulkan_device_handle.cpp
        vk_renderer_swap.cpp
        vk_renderer_desc_set.cpp
        vk_renderer_graphics_pipe.cpp
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
        -Wconversion -Wunreachable-code -fPIE

    PUBLIC
        -std=c++20)

target_link_libraries(Graphics
    PUBLIC
        ReadFile
        SDL3
        quill::quill
        volk
)
