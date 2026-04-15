add_library(Rendering)
target_sources(Rendering
    PRIVATE
        render.cpp

    PUBLIC
        FILE_SET HEADERS
        FILES
            render.hpp
)

target_compile_options(Rendering PRIVATE -Wall -Wextra -Wfloat-equal -Wundef
                       -Wshadow -Wpointer-arith -Wcast-align
                       -Wstrict-overflow=5 -Wwrite-strings -Wcast-qual -Wswitch-default
                       -Wconversion -Wunreachable-code)

find_package(quill REQUIRED)
find_library(ReadFile ${PROJECT_SOURCE_DIR}/lib/lib-readfile)
target_link_libraries(Rendering
    PUBLIC
        ReadFile
        SDL3
        quill::quill
        vulkan
)
