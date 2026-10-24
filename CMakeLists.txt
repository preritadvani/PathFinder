cmake_minimum_required(VERSION 3.16)
project(fx_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# All engine code is header-only, so it is one INTERFACE library.
add_library(fx_core INTERFACE)
target_include_directories(fx_core INTERFACE include)
target_compile_options(fx_core INTERFACE -Wall -Wextra)
target_link_libraries(fx_core INTERFACE Threads::Threads)

# The simulation program
add_executable(fx_main src/main.cpp)
target_link_libraries(fx_main PRIVATE fx_core)

# Tests (run with: ctest --test-dir build)
enable_testing()
add_executable(test_order_book tests/test_order_book.cpp)
target_link_libraries(test_order_book PRIVATE fx_core)
add_test(NAME order_book COMMAND test_order_book)
