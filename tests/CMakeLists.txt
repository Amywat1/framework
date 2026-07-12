set(WDF_ROOT ${CMAKE_SOURCE_DIR})

# 辅助宏：添加单元测试（每个测试只链接自身逻辑所需的最小源集）
# 用法：add_unit_test(<name> <test_src> [extra_src ...])
macro(add_unit_test name test_src)
    add_executable(${name}
        ${test_src}
        ${ARGN}
        ${WDF_ROOT}/third_party/unity/unity.c
    )
    target_include_directories(${name} PRIVATE
        ${WDF_ROOT}
        ${WDF_ROOT}/third_party/unity
    )
    target_link_libraries(${name} PRIVATE pthread)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -g)
    add_test(NAME ${name} COMMAND ${name})
endmacro()

# ---------------------------------------------------------------------------
# 单元测试
# 运行：cmake --build build -j4 && ctest --test-dir build -V
# ---------------------------------------------------------------------------

add_unit_test(test_event_bus
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/test_event_bus.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/time_util.c
)

add_unit_test(test_hal_io_sim
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_hal_io_sim.c
    ${WDF_ROOT}/adapters/outbound/hal/sim/hal_io_sim.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_sim_encoder_counter
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_sim_encoder_counter.c
    ${WDF_ROOT}/adapters/outbound/hal/sim/sim_encoder_counter.c
)

add_unit_test(test_hal_voice_sim
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_hal_voice_sim.c
    ${WDF_ROOT}/adapters/outbound/hal/sim/hal_voice_sim.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_machine_ops_port
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/test_machine_ops_port.c
    ${WDF_ROOT}/ports/outbound/machine/machine_ops_port.c
)

add_unit_test(test_op_mode_alarm_port
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/test_op_mode_alarm_port.c
    ${WDF_ROOT}/ports/outbound/safety/op_mode_alarm_port.c
)

add_unit_test(test_cloud_ports
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/test_cloud_ports.c
    ${WDF_ROOT}/ports/port_registry.c
)

add_unit_test(test_cloud_point_validate
    ${CMAKE_CURRENT_SOURCE_DIR}/cloud/test_cloud_point_validate.c
    ${WDF_ROOT}/cloud/cloud_point_validate.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_cloud_point_dispatch
    ${CMAKE_CURRENT_SOURCE_DIR}/cloud/test_cloud_point_dispatch.c
    ${WDF_ROOT}/cloud/cloud_point_dispatch.c
    ${WDF_ROOT}/common/point_table/point_table_from_json.c
    ${WDF_ROOT}/common/point_table/point_table_to_json.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)

add_unit_test(test_cloud_point_watcher
    ${CMAKE_CURRENT_SOURCE_DIR}/cloud/test_cloud_point_watcher.c
    ${WDF_ROOT}/cloud/cloud_point_watcher.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/time_util.c
)

add_unit_test(test_cloud_model_report_scheduler
    ${CMAKE_CURRENT_SOURCE_DIR}/cloud/test_cloud_model_report_scheduler.c
    ${WDF_ROOT}/cloud/cloud_model.c
    ${WDF_ROOT}/cloud/cloud_point_dispatch.c
    ${WDF_ROOT}/cloud/cloud_point_validate.c
    ${WDF_ROOT}/cloud/cloud_point_watcher.c
    ${WDF_ROOT}/application/orchestrators/report_scheduler.c
    ${WDF_ROOT}/common/point_table/point_table_from_json.c
    ${WDF_ROOT}/common/point_table/point_table_to_json.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/runtime/scheduler/periodic_task.c
    ${WDF_ROOT}/runtime/scheduler/thread_registry.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)

add_unit_test(test_alarm_registry
    ${CMAKE_CURRENT_SOURCE_DIR}/domain/test_alarm_registry.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_safety_posture
    ${CMAKE_CURRENT_SOURCE_DIR}/domain/test_safety_posture.c
    ${WDF_ROOT}/domain/safety/safety_posture/safety_posture.c
    ${WDF_ROOT}/application/alarm_event_bridge.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_alarm_event_bridge
    ${CMAKE_CURRENT_SOURCE_DIR}/application/test_alarm_event_bridge.c
    ${WDF_ROOT}/application/alarm_event_bridge.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_op_mode_bridge
    ${CMAKE_CURRENT_SOURCE_DIR}/application/test_op_mode_bridge.c
    ${WDF_ROOT}/application/op_mode_bridge.c
    ${WDF_ROOT}/domain/command_gateway/operational_mode.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/outbound/safety/op_mode_alarm_port.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_operational_mode
    ${CMAKE_CURRENT_SOURCE_DIR}/domain/test_operational_mode.c
    ${WDF_ROOT}/domain/command_gateway/operational_mode.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_device_control_patterns
    ${CMAKE_CURRENT_SOURCE_DIR}/domain/test_device_control_patterns.c
    ${WDF_ROOT}/domain/device_control/model/actuator_events.c
    ${WDF_ROOT}/domain/device_control/patterns/bidirectional_motion.c
    ${WDF_ROOT}/domain/device_control/patterns/continuous_rotary.c
    ${WDF_ROOT}/domain/device_control/patterns/fluid_path.c
    ${WDF_ROOT}/domain/device_control/patterns/interlocked_slots.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
)
target_compile_definitions(test_device_control_patterns PRIVATE FLUID_PATH_UNIT_TEST)

add_unit_test(test_wash_engine
    ${CMAKE_CURRENT_SOURCE_DIR}/domain/test_wash_engine.c
    ${WDF_ROOT}/domain/wash/engine/engine.c
    ${WDF_ROOT}/domain/wash/engine/engine_expr.c
    ${WDF_ROOT}/domain/wash/engine/engine_io.c
    ${WDF_ROOT}/domain/wash/model/engine_model.c
    ${WDF_ROOT}/domain/wash/model/engine_program_validate.c
    ${WDF_ROOT}/domain/wash/model/engine_program_manifest.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)

add_unit_test(test_side_effect_router
    ${CMAKE_CURRENT_SOURCE_DIR}/application/test_side_effect_router.c
    ${WDF_ROOT}/application/side_effect_router.c
    ${WDF_ROOT}/application/self_check_service.c
    ${WDF_ROOT}/domain/command_gateway/operational_mode.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/outbound/machine/machine_ops_port.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/tests/stubs/wash_orchestrator_stub.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_wash_orchestrator
    ${CMAKE_CURRENT_SOURCE_DIR}/application/test_wash_orchestrator.c
    ${WDF_ROOT}/application/orchestrators/wash_orchestrator.c
    ${WDF_ROOT}/adapters/outbound/storage/json/engine_program_json.c
    ${WDF_ROOT}/adapters/outbound/hal/sim/engine_io_sim.c
    ${WDF_ROOT}/ports/outbound/storage/engine_program_loader_port.c
    ${WDF_ROOT}/ports/outbound/machine/machine_ops_port.c
    ${WDF_ROOT}/runtime/scheduler/thread_registry.c
    ${WDF_ROOT}/runtime/scheduler/scheduler.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/domain/wash/engine/engine.c
    ${WDF_ROOT}/domain/wash/engine/engine_expr.c
    ${WDF_ROOT}/domain/wash/engine/engine_io.c
    ${WDF_ROOT}/domain/wash/model/engine_model.c
    ${WDF_ROOT}/domain/wash/model/engine_program_validate.c
    ${WDF_ROOT}/domain/wash/model/engine_program_manifest.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)
target_compile_definitions(test_wash_orchestrator PRIVATE
    PROGRAM_INTEGRITY_CHECK=0
    WASH_PROGRAM_NORMAL_PATH="/tmp/wdf_wash_orchestrator_program.json"
    WASH_PROGRAM_QUICK_PATH="/tmp/wdf_wash_orchestrator_program.json"
    TEST_WASH_PROGRAM_PATH="/tmp/wdf_wash_orchestrator_program.json"
)

add_unit_test(test_recovery_emergency
    ${CMAKE_CURRENT_SOURCE_DIR}/application/test_recovery_emergency.c
    ${WDF_ROOT}/application/recovery_service.c
    ${WDF_ROOT}/application/orchestrators/emergency_handler.c
    ${WDF_ROOT}/application/safety_deferred_stop.c
    ${WDF_ROOT}/adapters/outbound/safety/sim/hw_estop_sim.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/outbound/machine/machine_ops_port.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
)

add_unit_test(test_telemetry_projection
    ${CMAKE_CURRENT_SOURCE_DIR}/application/test_telemetry_projection.c
    ${WDF_ROOT}/application/wash_projection.c
    ${WDF_ROOT}/application/operational_projection.c
    ${WDF_ROOT}/application/safety_projection.c
    ${WDF_ROOT}/domain/telemetry/snapshot/wash_snapshot.c
    ${WDF_ROOT}/domain/telemetry/snapshot/operational_snapshot.c
    ${WDF_ROOT}/domain/telemetry/snapshot/safety_snapshot.c
    ${WDF_ROOT}/domain/command_gateway/operational_mode.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
)

add_unit_test(test_command_gateway
    ${CMAKE_CURRENT_SOURCE_DIR}/application/test_command_gateway.c
    ${WDF_ROOT}/application/command_gateway.c
    ${WDF_ROOT}/application/side_effect_router.c
    ${WDF_ROOT}/application/self_check_service.c
    ${WDF_ROOT}/domain/command_gateway/operational_mode.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/ports/outbound/machine/machine_ops_port.c
    ${WDF_ROOT}/tests/stubs/wash_orchestrator_stub.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_pulse_out
    ${CMAKE_CURRENT_SOURCE_DIR}/common/test_pulse_out.c
    ${WDF_ROOT}/common/pulse_out.c
    ${WDF_ROOT}/common/time_util.c
)

add_unit_test(test_io_handle
    ${CMAKE_CURRENT_SOURCE_DIR}/common/test_io_handle.c
)

add_unit_test(test_util_crc
    ${CMAKE_CURRENT_SOURCE_DIR}/common/test_util_crc.c
    ${WDF_ROOT}/common/util_crc.c
)

add_unit_test(test_util_fifo
    ${CMAKE_CURRENT_SOURCE_DIR}/common/test_util_fifo.c
    ${WDF_ROOT}/common/util_fifo.c
)

add_unit_test(test_point_table
    ${CMAKE_CURRENT_SOURCE_DIR}/common/test_point_table.c
    ${WDF_ROOT}/common/point_table/point_table_from_json.c
    ${WDF_ROOT}/common/point_table/point_table_to_json.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

add_unit_test(test_json_param_store
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_json_param_store.c
    ${WDF_ROOT}/adapters/outbound/storage/json/json_param_store.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)
target_compile_definitions(test_json_param_store PRIVATE
    PARAM_STORE_JSON_FILE_PATH="${CMAKE_BINARY_DIR}/tests/param_store_test.json"
)

add_unit_test(test_json_deploy_store
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_json_deploy_store.c
    ${WDF_ROOT}/adapters/outbound/storage/json/json_deploy_store.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)
target_compile_definitions(test_json_deploy_store PRIVATE
    DEPLOY_STORE_JSON_FILE_PATH="${CMAKE_BINARY_DIR}/tests/deploy_store_test.json"
)

add_unit_test(test_engine_program_json
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_engine_program_json.c
    ${WDF_ROOT}/adapters/outbound/storage/json/engine_program_json.c
    ${WDF_ROOT}/adapters/outbound/hal/sim/engine_io_sim.c
    ${WDF_ROOT}/ports/outbound/storage/engine_program_loader_port.c
    ${WDF_ROOT}/domain/wash/engine/engine.c
    ${WDF_ROOT}/domain/wash/engine/engine_expr.c
    ${WDF_ROOT}/domain/wash/engine/engine_io.c
    ${WDF_ROOT}/domain/wash/model/engine_model.c
    ${WDF_ROOT}/domain/wash/model/engine_program_validate.c
    ${WDF_ROOT}/domain/wash/model/engine_program_manifest.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)

add_unit_test(test_scheduler
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/test_scheduler.c
    ${WDF_ROOT}/runtime/scheduler/thread_registry.c
    ${WDF_ROOT}/runtime/scheduler/scheduler.c
    ${WDF_ROOT}/runtime/scheduler/periodic_task.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_svc_param
    ${CMAKE_CURRENT_SOURCE_DIR}/services/test_svc_param.c
    ${WDF_ROOT}/services/param/svc_param.c
    ${WDF_ROOT}/adapters/outbound/storage/json/json_param_store.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)
target_compile_definitions(test_svc_param PRIVATE
    PARAM_STORE_JSON_FILE_PATH="${CMAKE_BINARY_DIR}/tests/svc_param_test.json"
)

add_unit_test(test_safety_thread
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/test_safety_thread.c
    ${WDF_ROOT}/runtime/platform/safety_thread.c
    ${WDF_ROOT}/runtime/platform/device_safety_actuator.c
    ${WDF_ROOT}/runtime/scheduler/thread_registry.c
    ${WDF_ROOT}/runtime/scheduler/scheduler.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/ports/outbound/safety/hw_estop_port.c
    ${WDF_ROOT}/ports/outbound/safety/safety_cutout_port.c
    ${WDF_ROOT}/adapters/outbound/safety/sim/hw_estop_sim.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
)
