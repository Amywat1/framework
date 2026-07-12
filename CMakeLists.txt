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
option(WDF_ENABLE_SNACK_MODBUS_PROVIDER "Build Snack Modbus HAL providers" OFF)
option(WDF_ENABLE_SNACK_IO_EXP_PROVIDER "Build Snack io_exp HAL provider" OFF)
set(WDF_MCC_ROOT "" CACHE PATH "Path to motor_control_core SDK root")
set(WDF_IO_EXP_ROOT "" CACHE PATH "Path to io_exp SDK root")
set(WDF_IO_EXP_LIBRARY "" CACHE FILEPATH "Path to io_exp SDK library")

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

if(WDF_ENABLE_SNACK_IO_EXP_PROVIDER)
    if(NOT WDF_IO_EXP_ROOT)
        message(FATAL_ERROR "WDF_IO_EXP_ROOT must point to io_exp SDK when WDF_ENABLE_SNACK_IO_EXP_PROVIDER=ON")
    endif()
    if(NOT EXISTS "${WDF_IO_EXP_ROOT}/include/io_exp/demo.h")
        message(FATAL_ERROR "io_exp header not found: ${WDF_IO_EXP_ROOT}/include/io_exp/demo.h")
    endif()
    if(NOT EXISTS "${WDF_IO_EXP_ROOT}/include/io_exp/slave.h")
        message(FATAL_ERROR "io_exp header not found: ${WDF_IO_EXP_ROOT}/include/io_exp/slave.h")
    endif()
    if(NOT WDF_IO_EXP_LIBRARY OR NOT EXISTS "${WDF_IO_EXP_LIBRARY}")
        message(FATAL_ERROR "WDF_IO_EXP_LIBRARY must point to the io_exp SDK library")
    endif()

    add_library(wdf_hal_io_exp_snack STATIC
        adapters/outbound/hal/providers/snack/io_exp/io_exp_driver.c
        adapters/outbound/hal/providers/snack/io_exp/snack_io_adapter.c
    )
    target_include_directories(wdf_hal_io_exp_snack PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${WDF_IO_EXP_ROOT}/include
    )
    target_link_libraries(wdf_hal_io_exp_snack PUBLIC
        ${WDF_IO_EXP_LIBRARY}
        pthread
    )
endif()

if(WDF_ENABLE_SNACK_MODBUS_PROVIDER)
    find_path(WDF_LIBMODBUS_INCLUDE_DIR modbus/modbus-rtu.h)
    find_library(WDF_LIBMODBUS_LIBRARY modbus)
    if(NOT WDF_LIBMODBUS_INCLUDE_DIR)
        message(FATAL_ERROR "libmodbus header not found: modbus/modbus-rtu.h")
    endif()
    if(NOT WDF_LIBMODBUS_LIBRARY)
        message(FATAL_ERROR "libmodbus library not found")
    endif()

    add_library(wdf_hal_voice_snack STATIC
        adapters/outbound/hal/providers/snack/modbus/drv_modbus_link.c
        adapters/outbound/hal/providers/snack/modbus/drv_voice.c
        adapters/outbound/hal/providers/snack/modbus/snack_voice_adapter.c
    )
    target_include_directories(wdf_hal_voice_snack PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${WDF_LIBMODBUS_INCLUDE_DIR}
    )
    target_link_libraries(wdf_hal_voice_snack PUBLIC
        ${WDF_LIBMODBUS_LIBRARY}
        pthread
    )

    if(WDF_ENABLE_SNACK_IO_EXP_PROVIDER)
        add_library(wdf_hal_vfd_snack STATIC
            adapters/outbound/hal/providers/snack/modbus/drv_modbus_link.c
            adapters/outbound/hal/providers/snack/modbus/drv_vfd.c
            adapters/outbound/hal/providers/snack/modbus/snack_vfd_backend.c
            adapters/outbound/hal/components/vfd_manager/hal_vfd_manager.c
        )
        target_include_directories(wdf_hal_vfd_snack PUBLIC
            ${CMAKE_SOURCE_DIR}
            ${WDF_LIBMODBUS_INCLUDE_DIR}
            ${WDF_IO_EXP_ROOT}/include
        )
        target_link_libraries(wdf_hal_vfd_snack PUBLIC
            wdf_hal_io_exp_snack
            ${WDF_LIBMODBUS_LIBRARY}
            pthread
        )
    endif()
endif()

if(WDF_BUILD_DEMO)
    add_subdirectory(demo)
endif()

if(WDF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
