cmake_minimum_required(VERSION 3.10.0)

project(Demo VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")

# 位于 framework 仓库 examples/Demo/，仓库根目录为 ../..
set(FW_ROOT ${CMAKE_SOURCE_DIR}/../..)

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${FW_ROOT}
    OUTPUT_VARIABLE FW_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT FW_GIT_HASH)
    set(FW_GIT_HASH "unknown")
endif()

add_compile_definitions(
    SW_GIT_HASH="${FW_GIT_HASH}"
    PARAM_STORE_JSON_FILE_PATH="/tmp/demo/params.json"
    DEPLOY_STORE_JSON_FILE_PATH="/tmp/demo/device.json"
)

file(GLOB demo_app_src CONFIGURE_DEPENDS app/*.c)
file(GLOB demo_wiring_src CONFIGURE_DEPENDS wiring/wiring_sim.c)

set(fw_common_src
    ${FW_ROOT}/framework/common/log.c
    ${FW_ROOT}/framework/common/time_util.c
    ${FW_ROOT}/framework/common/pulse_out.c
)

set(fw_runtime_src
    ${FW_ROOT}/framework/runtime/event_bus/event_bus.c
)

set(fw_ports_src
    ${FW_ROOT}/framework/ports/port_registry.c
)

set(fw_adapters_src
    ${FW_ROOT}/framework/adapters/outbound/hal/sim/hal_io_sim.c
    ${FW_ROOT}/framework/adapters/outbound/storage/json/json_param_store.c
    ${FW_ROOT}/framework/adapters/outbound/storage/json/json_deploy_store.c
)

set(demo_sources
    ${demo_app_src}
    ${demo_wiring_src}
    ${fw_common_src}
    ${fw_runtime_src}
    ${fw_ports_src}
    ${fw_adapters_src}
    ${FW_ROOT}/third_party/cJSON/cJSON.c
)

add_executable(Demo ${demo_sources})

target_include_directories(Demo PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${FW_ROOT}
)

target_compile_options(Demo PRIVATE -Wall -Wextra -Werror -g)
target_link_libraries(Demo PRIVATE pthread)

message(STATUS "Demo example: FW_ROOT=${FW_ROOT} (${FW_GIT_HASH})")
