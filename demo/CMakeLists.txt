set(WDF_ROOT ${CMAKE_SOURCE_DIR})

add_compile_definitions(
    PARAM_STORE_JSON_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/params.json"
    DEPLOY_STORE_JSON_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/device.json"
)

file(GLOB _demo_app CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/app/*.c)
file(GLOB _demo_wiring CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/wiring/wiring_sim.c)

# 已迁入框架源文件（按模块分组，迁入新模块时在此追加）
set(_fw_common_src
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/pulse_out.c
)

set(_fw_runtime_src
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/runtime/scheduler/thread_registry.c
    ${WDF_ROOT}/runtime/scheduler/scheduler.c
    ${WDF_ROOT}/runtime/scheduler/periodic_task.c
    ${WDF_ROOT}/runtime/platform/safety_thread.c
    ${WDF_ROOT}/runtime/platform/device_safety_actuator.c
)

set(_fw_ports_src
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/ports/outbound/safety/hw_estop_port.c
    ${WDF_ROOT}/ports/outbound/safety/safety_cutout_port.c
    ${WDF_ROOT}/ports/outbound/safety/op_mode_alarm_port.c
    ${WDF_ROOT}/ports/outbound/machine/machine_ops_port.c
)

set(_fw_services_src
    ${WDF_ROOT}/services/param/svc_param.c
)

set(_fw_adapters_src
    ${WDF_ROOT}/adapters/outbound/hal/sim/hal_io_sim.c
    ${WDF_ROOT}/adapters/outbound/hal/sim/hal_voice_sim.c
    ${WDF_ROOT}/adapters/outbound/safety/sim/hw_estop_sim.c
    ${WDF_ROOT}/adapters/outbound/storage/json/json_param_store.c
    ${WDF_ROOT}/adapters/outbound/storage/json/json_deploy_store.c
)

set(_fw_third_party_src
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)

add_executable(wdf_smoke
    ${_demo_app}
    ${_demo_wiring}
    ${_fw_common_src}
    ${_fw_runtime_src}
    ${_fw_ports_src}
    ${_fw_services_src}
    ${_fw_adapters_src}
    ${_fw_third_party_src}
)

target_include_directories(wdf_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${WDF_ROOT}
)

target_compile_options(wdf_smoke PRIVATE -Wall -Wextra -Werror -g)
target_link_libraries(wdf_smoke PRIVATE pthread)
