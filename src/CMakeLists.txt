find_library(Graphics graphics)

add_subdirectory(graphics)
add_library(App)
target_sources(App
    PRIVATE
        app.cpp

    PUBLIC
        FILE_SET HEADERS
        FILES app.hpp
)

target_link_libraries(App
    PRIVATE
        SDL3
        Graphics
)

target_compile_options(App
    PRIVATE
        -Wall -Wextra -Wfloat-equal -Wundef
        -Wshadow -Wpointer-arith -Wcast-align
        -Wstrict-overflow=5 -Wwrite-strings -Wcast-qual
        -Wswitch-default -Wconversion -Wunreachable-code

    PUBLIC
        -std=c++20
)
