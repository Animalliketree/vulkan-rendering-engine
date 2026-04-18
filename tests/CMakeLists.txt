find_package(Catch2 3 REQUIRED)
add_executable(Tests)
target_sources(Tests PRIVATE test_graphics.cpp)
target_link_libraries(Tests PRIVATE Catch2::Catch2WithMain)
