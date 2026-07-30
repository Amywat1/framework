set(WDF_ROOT ${CMAKE_SOURCE_DIR})

# 辅助宏：添加单元测试（每个测试只链接自身逻辑所需的最小源集）
# 用法：add_unit_test(<name> <test_src> [extra_src ...])
macro(add_unit_test name test_src)
    add_executable(${name}
        ${test_src}
        ${ARGN}
        ${WDF_ROOT}/common/trace_context.c
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

add_unit_test(test_log
    ${CMAKE_CURRENT_SOURCE_DIR}/common/test_log.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_log_multi_sink
    ${CMAKE_CURRENT_SOURCE_DIR}/common/test_log_multi_sink.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_event_bus
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/test_event_bus.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/time_util.c
)

add_unit_test(test_hal_io_sim
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_hal_io_sim.c
    ${WDF_ROOT}/adapters/outbound/hal/sim/hal_io_sim.c
    ${WDF_ROOT}/ports/port_registry_hal.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
)
target_compile_definitions(test_hal_io_sim PRIVATE HAL_IO_SIM_UNIT_TEST)

add_unit_test(test_hal_sensor_filter
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_hal_sensor_filter.c
    ${WDF_ROOT}/adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.c
    ${WDF_ROOT}/adapters/outbound/hal/sim/hal_io_sim.c
    ${WDF_ROOT}/runtime/scheduler/periodic_task.c
    ${WDF_ROOT}/runtime/scheduler/thread_registry.c
    ${WDF_ROOT}/ports/port_registry_hal.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
)
target_compile_definitions(test_hal_sensor_filter PRIVATE HAL_SENSOR_FILTER_UNIT_TEST HAL_IO_SIM_UNIT_TEST)

add_unit_test(test_hal_vfd_manager
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_hal_vfd_manager.c
    ${WDF_ROOT}/adapters/outbound/hal/components/vfd_manager/hal_vfd_manager.c
    ${WDF_ROOT}/common/pulse_out.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/runtime/scheduler/periodic_task.c
    ${WDF_ROOT}/runtime/scheduler/thread_registry.c
    ${WDF_ROOT}/ports/port_registry_hal.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/common/log.c
)
target_compile_definitions(test_hal_vfd_manager PRIVATE HAL_VFD_MANAGER_UNIT_TEST)

add_unit_test(test_hal_motor_exec_mcc
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_hal_motor_exec_mcc.c
    ${WDF_ROOT}/adapters/outbound/hal/providers/mcc/hal_motor_exec_adapter.c
    ${WDF_ROOT}/tests/stubs/mcc/motor_control_core_fake.c
)

add_unit_test(test_motor_executor_position
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_motor_executor_position.c
    ${WDF_ROOT}/adapters/outbound/hal/providers/mcc/motor_executor.c
)

add_unit_test(test_hal_voice_sim
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_hal_voice_sim.c
    ${WDF_ROOT}/adapters/outbound/hal/sim/hal_voice_sim.c
    ${WDF_ROOT}/ports/port_registry_hal.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/common/log.c
)
target_compile_definitions(test_hal_voice_sim PRIVATE HAL_VOICE_SIM_UNIT_TEST)

add_unit_test(test_snack_voice_adapter
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_snack_voice_adapter.c
    ${WDF_ROOT}/adapters/outbound/hal/providers/snack/modbus/drv_voice.c
    ${WDF_ROOT}/adapters/outbound/hal/providers/snack/modbus/snack_voice_adapter.c
    ${WDF_ROOT}/tests/stubs/snack/drv_modbus_link_fake.c
    ${WDF_ROOT}/ports/port_registry_hal.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/common/log.c
)
target_compile_definitions(test_snack_voice_adapter PRIVATE SNACK_VOICE_ADAPTER_UNIT_TEST)

add_unit_test(test_snack_io_exp_adapter
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_snack_io_exp_adapter.c
    ${WDF_ROOT}/adapters/outbound/hal/providers/snack/io_exp/io_exp_driver.c
    ${WDF_ROOT}/adapters/outbound/hal/providers/snack/io_exp/snack_io_adapter.c
    ${WDF_ROOT}/tests/stubs/io_exp/io_exp_fake.c
    ${WDF_ROOT}/ports/port_registry_hal.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
)
target_include_directories(test_snack_io_exp_adapter PRIVATE
    ${WDF_ROOT}/tests/stubs
)
target_compile_definitions(test_snack_io_exp_adapter PRIVATE SNACK_IO_ADAPTER_UNIT_TEST)

add_unit_test(test_snack_vfd_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_snack_vfd_backend.c
    ${WDF_ROOT}/adapters/outbound/hal/providers/snack/modbus/drv_vfd.c
    ${WDF_ROOT}/adapters/outbound/hal/providers/snack/modbus/snack_vfd_backend.c
    ${WDF_ROOT}/adapters/outbound/hal/providers/snack/io_exp/io_exp_driver.c
    ${WDF_ROOT}/adapters/outbound/hal/components/vfd_manager/hal_vfd_manager.c
    ${WDF_ROOT}/tests/stubs/snack/drv_modbus_link_fake.c
    ${WDF_ROOT}/tests/stubs/io_exp/io_exp_fake.c
    ${WDF_ROOT}/common/pulse_out.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/runtime/scheduler/periodic_task.c
    ${WDF_ROOT}/runtime/scheduler/thread_registry.c
    ${WDF_ROOT}/ports/port_registry_hal.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/common/log.c
)
target_include_directories(test_snack_vfd_backend PRIVATE
    ${WDF_ROOT}/tests/stubs
)
target_compile_definitions(test_snack_vfd_backend PRIVATE HAL_VFD_MANAGER_UNIT_TEST)
target_compile_definitions(test_snack_vfd_backend PRIVATE SNACK_VFD_BACKEND_UNIT_TEST)

add_unit_test(test_snack_cloud_adapters
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_snack_cloud_adapters.c
    ${WDF_ROOT}/adapters/outbound/cloud/providers/snack/snack_cloud_link_adapter.c
    ${WDF_ROOT}/adapters/outbound/cloud/providers/snack/snack_cloud_report_adapter.c
    ${WDF_ROOT}/adapters/inbound/cloud/providers/snack/snack_cloud_command_adapter.c
    ${WDF_ROOT}/tests/stubs/snack_cloud/snack_mqtt_fake.c
    ${WDF_ROOT}/tests/stubs/snack_cloud/cloud_model_fake.c
    ${WDF_ROOT}/common/point_table/point_table_from_json.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/ports/port_registry_cloud.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)

add_unit_test(test_machine_ops_port
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/test_machine_ops_port.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_op_mode_alarm_port
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/test_op_mode_alarm_port.c
    ${WDF_ROOT}/adapters/outbound/safety/sim/safety_weak_defaults.c
)

add_unit_test(test_cloud_ports
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/test_cloud_ports.c
    ${WDF_ROOT}/ports/port_registry_cloud.c
    ${WDF_ROOT}/ports/port_registry_infra.c
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
    ${WDF_ROOT}/ports/port_registry_infra.c
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
    ${WDF_ROOT}/ports/port_registry_cloud.c
    ${WDF_ROOT}/ports/port_registry_infra.c
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
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_safety_posture
    ${CMAKE_CURRENT_SOURCE_DIR}/domain/test_safety_posture.c
    ${WDF_ROOT}/adapters/inbound/event/alarm_event_bridge.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/runtime/scheduler/periodic_task.c
    ${WDF_ROOT}/runtime/scheduler/thread_registry.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_alarm_event_bridge
    ${CMAKE_CURRENT_SOURCE_DIR}/application/test_alarm_event_bridge.c
    ${WDF_ROOT}/adapters/inbound/event/alarm_event_bridge.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/runtime/scheduler/periodic_task.c
    ${WDF_ROOT}/runtime/scheduler/thread_registry.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_alarm_reeval_bridge
    ${CMAKE_CURRENT_SOURCE_DIR}/application/test_alarm_reeval_bridge.c
    ${WDF_ROOT}/adapters/inbound/event/alarm_reeval_bridge.c
    ${WDF_ROOT}/domain/device_control/model/actuator_events.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_op_mode_bridge
    ${CMAKE_CURRENT_SOURCE_DIR}/application/test_op_mode_bridge.c
    ${WDF_ROOT}/adapters/inbound/event/op_mode_bridge.c
    ${WDF_ROOT}/domain/op_mode/operational_mode.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/adapters/outbound/safety/sim/safety_weak_defaults.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_operational_mode
    ${CMAKE_CURRENT_SOURCE_DIR}/domain/test_operational_mode.c
    ${WDF_ROOT}/domain/op_mode/operational_mode.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_device_control_patterns
    ${CMAKE_CURRENT_SOURCE_DIR}/domain/test_device_control_patterns.c
    ${WDF_ROOT}/domain/device_control/model/actuator_events.c
    ${WDF_ROOT}/domain/device_control/patterns/motor_axis.c
    ${WDF_ROOT}/domain/device_control/patterns/fluid_path.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
)
target_compile_definitions(test_device_control_patterns PRIVATE FLUID_PATH_UNIT_TEST)

add_unit_test(test_program_engine
    ${CMAKE_CURRENT_SOURCE_DIR}/domain/test_program_engine.c
    ${WDF_ROOT}/domain/program_engine/engine/engine.c
    ${WDF_ROOT}/domain/program_engine/engine/engine_expr.c
    ${WDF_ROOT}/domain/program_engine/engine/engine_io.c
    ${WDF_ROOT}/domain/program_engine/engine/engine_var.c
    ${WDF_ROOT}/domain/program_engine/engine/engine_actuator.c
    ${WDF_ROOT}/adapters/outbound/hal/sim/engine_actuator_sim.c
    ${WDF_ROOT}/domain/program_engine/engine/engine_profile.c
    ${WDF_ROOT}/domain/program_engine/model/engine_model.c
    ${WDF_ROOT}/domain/program_engine/model/engine_program_validate.c
    ${WDF_ROOT}/domain/program_engine/model/engine_program_manifest.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)

add_unit_test(test_side_effect_router
    ${CMAKE_CURRENT_SOURCE_DIR}/application/test_side_effect_router.c
    ${WDF_ROOT}/application/side_effect_router.c
    ${WDF_ROOT}/application/self_check_service.c
    ${WDF_ROOT}/domain/op_mode/operational_mode.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/tests/stubs/wash_ops_stub.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_engine_session
    ${CMAKE_CURRENT_SOURCE_DIR}/application/test_engine_session.c
    ${WDF_ROOT}/application/engine_session/engine_session.c
    ${WDF_ROOT}/adapters/outbound/storage/json/engine_program_json.c
    ${WDF_ROOT}/adapters/outbound/hal/sim/engine_io_sim.c
    ${WDF_ROOT}/domain/program_engine/engine/engine_actuator.c
    ${WDF_ROOT}/adapters/outbound/hal/sim/engine_actuator_sim.c
    ${WDF_ROOT}/ports/outbound/storage/engine_program_loader_port.c
    ${WDF_ROOT}/runtime/scheduler/thread_registry.c
    ${WDF_ROOT}/runtime/scheduler/scheduler.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/domain/program_engine/engine/engine.c
    ${WDF_ROOT}/domain/program_engine/engine/engine_expr.c
    ${WDF_ROOT}/domain/program_engine/engine/engine_io.c
    ${WDF_ROOT}/domain/program_engine/engine/engine_var.c
    ${WDF_ROOT}/domain/program_engine/engine/engine_profile.c
    ${WDF_ROOT}/domain/program_engine/model/engine_model.c
    ${WDF_ROOT}/domain/program_engine/model/engine_program_validate.c
    ${WDF_ROOT}/domain/program_engine/model/engine_program_manifest.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)
target_compile_definitions(test_engine_session PRIVATE
    TEST_ENGINE_SESSION_PROGRAM_PATH="/tmp/wdf_engine_session_program.json"
)

add_unit_test(test_recovery_emergency
    ${CMAKE_CURRENT_SOURCE_DIR}/application/test_recovery_emergency.c
    ${WDF_ROOT}/application/recovery_service.c
    ${WDF_ROOT}/application/orchestrators/safety_cutout_coordinator.c
    ${WDF_ROOT}/application/orchestrators/abort_home_coordinator.c
    ${WDF_ROOT}/adapters/outbound/safety/sim/hw_estop_sim.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
)

add_unit_test(test_telemetry_projection
    ${CMAKE_CURRENT_SOURCE_DIR}/application/test_telemetry_projection.c
    ${WDF_ROOT}/application/telemetry_projection.c
    ${WDF_ROOT}/domain/telemetry/device_snapshot.c
    ${WDF_ROOT}/domain/op_mode/operational_mode.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
)

add_unit_test(test_command_gateway
    ${CMAKE_CURRENT_SOURCE_DIR}/application/test_command_gateway.c
    ${WDF_ROOT}/application/command_gateway.c
    ${WDF_ROOT}/application/side_effect_router.c
    ${WDF_ROOT}/application/self_check_service.c
    ${WDF_ROOT}/domain/op_mode/operational_mode.c
    ${WDF_ROOT}/domain/safety/alarm_registry/alarm_registry.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/tests/stubs/wash_ops_stub.c
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
    ${WDF_ROOT}/ports/port_registry_cloud.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)
target_compile_definitions(test_json_param_store PRIVATE
    PARAM_STORE_JSON_FILE_PATH="${CMAKE_BINARY_DIR}/tests/param_store_test.json"
)

add_unit_test(test_json_deploy_store
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_json_deploy_store.c
    ${WDF_ROOT}/adapters/outbound/storage/json/json_deploy_store.c
    ${WDF_ROOT}/ports/port_registry_cloud.c
    ${WDF_ROOT}/ports/port_registry_infra.c
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
    ${WDF_ROOT}/domain/program_engine/engine/engine_actuator.c
    ${WDF_ROOT}/adapters/outbound/hal/sim/engine_actuator_sim.c
    ${WDF_ROOT}/ports/outbound/storage/engine_program_loader_port.c
    ${WDF_ROOT}/domain/program_engine/engine/engine.c
    ${WDF_ROOT}/domain/program_engine/engine/engine_expr.c
    ${WDF_ROOT}/domain/program_engine/engine/engine_io.c
    ${WDF_ROOT}/domain/program_engine/engine/engine_var.c
    ${WDF_ROOT}/domain/program_engine/engine/engine_profile.c
    ${WDF_ROOT}/domain/program_engine/model/engine_model.c
    ${WDF_ROOT}/domain/program_engine/model/engine_program_validate.c
    ${WDF_ROOT}/domain/program_engine/model/engine_program_manifest.c
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
    ${WDF_ROOT}/ports/port_registry_cloud.c
    ${WDF_ROOT}/ports/port_registry_infra.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)
target_compile_definitions(test_svc_param PRIVATE
    PARAM_STORE_JSON_FILE_PATH="${CMAKE_BINARY_DIR}/tests/svc_param_test.json"
)

add_unit_test(test_dev_ctx
    ${CMAKE_CURRENT_SOURCE_DIR}/services/test_dev_ctx.c
    ${WDF_ROOT}/services/dev_ctx/dev_ctx.c
    ${WDF_ROOT}/domain/telemetry/device_snapshot.c
    ${WDF_ROOT}/ports/port_registry_cloud.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_safety_thread
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/test_safety_thread.c
    ${WDF_ROOT}/runtime/platform/safety_thread.c
    ${WDF_ROOT}/runtime/scheduler/thread_registry.c
    ${WDF_ROOT}/runtime/scheduler/scheduler.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/adapters/outbound/safety/sim/hw_estop_sim.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/common/time_util.c
)

add_unit_test(test_observation_core
    ${CMAKE_CURRENT_SOURCE_DIR}/observability/test_observation_core.c
    ${WDF_ROOT}/observability/core/observation.c
    ${WDF_ROOT}/observability/recorder/blackbox_recorder.c
    ${WDF_ROOT}/common/time_util.c
)
