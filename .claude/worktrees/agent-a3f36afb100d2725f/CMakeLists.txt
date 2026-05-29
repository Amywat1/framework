cmake_minimum_required(VERSION 3.20)

project(wash_controller C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
