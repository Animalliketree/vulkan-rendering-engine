add_library(VulkanRenderer)
target_sources(VulkanRenderer
    PRIVATE
        render.cpp

    PUBLIC
        FILE_SET HEADERS
        FILES
            render.hpp
)

target_compile_options(VulkanRenderer
    PRIVATE
        -Wall -Wextra -Wfloat-equal -Wundef
        -Wshadow -Wpointer-arith -Wcast-align
        -Wstrict-overflow=5 -Wwrite-strings -Wcast-qual -Wswitch-default
        -Wconversion -Wunreachable-code
        
    PUBLIC
        -std=c++20)

find_package(quill REQUIRED)
target_link_libraries(VulkanRenderer
    PUBLIC
        ReadFile
        SDL3
        quill::quill
        vulkan
)
