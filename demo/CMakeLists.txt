set(WDF_ROOT ${CMAKE_SOURCE_DIR})

add_compile_definitions(
    PARAM_STORE_JSON_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/params.json"
    DEPLOY_STORE_JSON_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/device.json"
)

file(GLOB _demo_app CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/app/*.c)
set(_demo_wiring
    ${CMAKE_CURRENT_SOURCE_DIR}/wiring/wiring_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wiring/project_hooks_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wiring/demo_machine_ops.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wiring/demo_alarm_catalog.c
)

set(_fw_common_src
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/trace_context.c
)

set(_fw_runtime_src
    ${WDF_ROOT}/runtime/bootstrap/bootstrap.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/runtime/scheduler/thread_registry.c
    ${WDF_ROOT}/runtime/scheduler/scheduler.c
    ${WDF_ROOT}/runtime/scheduler/periodic_task.c
    ${WDF_ROOT}/runtime/platform/safety_thread.c
)

set(_fw_application_src
    ${WDF_ROOT}/adapters/inbound/event/alarm_event_bridge.c
    ${WDF_ROOT}/adapters/inbound/event/alarm_lifecycle_bridge.c
    ${WDF_ROOT}/application/command_gateway.c
    ${WDF_ROOT}/adapters/inbound/event/op_mode_bridge.c
    ${WDF_ROOT}/application/self_check_service.c
    ${WDF_ROOT}/application/side_effect_router.c
    ${WDF_ROOT}/application/telemetry_projection.c
    ${WDF_ROOT}/tests/stubs/wash_orchestrator_stub.c
)

set(_fw_domain_src
    ${WDF_ROOT}/domain/op_mode/operational_mode.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/domain/telemetry/device_snapshot.c
)

set(_fw_ports_src
    ${WDF_ROOT}/ports/port_registry_hal.c
    ${WDF_ROOT}/ports/port_registry_cloud.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/adapters/outbound/safety/sim/safety_weak_defaults.c
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
    ${_fw_application_src}
    ${_fw_domain_src}
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
