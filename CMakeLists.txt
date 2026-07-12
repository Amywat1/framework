cmake_minimum_required(VERSION 3.10.0)

project(wash_device_framework VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")

include(${CMAKE_SOURCE_DIR}/cmake/generate_version.cmake)
include_directories(${WDF_GENERATED_INCLUDE_DIR})

option(WDF_BUILD_DEMO "Build in-repo smoke demo" ON)
option(WDF_BUILD_TESTS "Build unit tests" ON)
option(WDF_ENABLE_MCC_PROVIDER "Build MCC motor executor HAL provider" OFF)
set(WDF_MCC_ROOT "" CACHE PATH "Path to motor_control_core SDK root")

if(WDF_ENABLE_MCC_PROVIDER)
    if(NOT WDF_MCC_ROOT)
        message(FATAL_ERROR "WDF_MCC_ROOT must point to motor_control_core SDK when WDF_ENABLE_MCC_PROVIDER=ON")
    endif()
    if(NOT EXISTS "${WDF_MCC_ROOT}/include/motor/motor_executor.h")
        message(FATAL_ERROR "MCC header not found: ${WDF_MCC_ROOT}/include/motor/motor_executor.h")
    endif()
    if(NOT EXISTS "${WDF_MCC_ROOT}/lib/libmotor_control_core.a")
        message(FATAL_ERROR "MCC static library not found: ${WDF_MCC_ROOT}/lib/libmotor_control_core.a")
    endif()

    add_library(wdf_hal_motor_exec_mcc STATIC
        adapters/outbound/hal/providers/mcc/hal_motor_exec_adapter.c
    )
    target_include_directories(wdf_hal_motor_exec_mcc PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${WDF_MCC_ROOT}/include
    )
    target_link_libraries(wdf_hal_motor_exec_mcc PUBLIC
        ${WDF_MCC_ROOT}/lib/libmotor_control_core.a
    )
endif()

if(WDF_BUILD_DEMO)
    add_subdirectory(demo)
endif()

if(WDF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
