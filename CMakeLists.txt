cmake_minimum_required(VERSION 3.10.0)

project(wash_device_framework VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")

include(${CMAKE_SOURCE_DIR}/cmake/generate_version.cmake)
include_directories(${WDF_GENERATED_INCLUDE_DIR})

option(WDF_BUILD_DEMO "Build in-repo smoke demo" ON)
option(WDF_BUILD_TESTS "Build unit tests" ON)

if(WDF_BUILD_DEMO)
    add_subdirectory(demo)
endif()

if(WDF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
