add_library(ReadFile)
target_sources(ReadFile
    PRIVATE
        readfile.cpp

    PUBLIC
        FILE_SET HEADERS
        FILES
            readfile.hpp
)

target_compile_options(ReadFile PRIVATE -Wall)
target_include_directories(ReadFile PUBLIC .)