set(WASH_CORE_SOURCES
    ../third_party/cjson/cJSON.c
    domain/model/segment_motion_plan.c
    domain/model/position_trigger.c
    domain/model/conditional_control.c
    domain/model/exit_action.c
    domain/model/segment_exception_policy.c
    domain/model/wash_segment.c
    domain/model/wash_program.c
    domain/model/vehicle_type.c
    domain/model/program_validation.c
    domain/model/program_snapshot.c
    domain/model/state_transition_record.c
    domain/model/wait_condition.c
    domain/model/wash_execution.c
    domain/model/wash_session.c
    domain/model/wash_trigger_event.c
    domain/services/fault_policy_service.c
    domain/services/program_snapshot_service.c
    domain/services/recovery_state_machine.c
    domain/services/segment_control_service.c
    domain/services/trigger_priority_service.c
    domain/services/wait_timeout_service.c
    domain/services/wash_execution_service.c
    domain/services/wash_session_state_machine.c
    application/coordinators/controller_runtime.c
    application/coordinators/system_context.c
    application/coordinators/runtime_event_recorder.c
    application/coordinators/runtime_result_projection.c
    application/use_cases/process_formal_command.c
    application/use_cases/process_wash_trigger.c
    application/use_cases/query_wash_session_status.c
    adapters/inbound/cli_command_adapter.c
    adapters/outbound/file_program_repository.c
    adapters/config/json_program_parser.c
    adapters/logging/file_event_logger.c
    platform/linux/controller_scheduler_linux.c
    platform/linux/main_loop.c
    platform/drivers/simulated_gantry_driver.c
    platform/drivers/simulated_brush_driver.c
    platform/drivers/simulated_chemical_driver.c
    platform/drivers/simulated_ro_water_driver.c
    platform/drivers/simulated_dryer_driver.c
    platform/drivers/simulated_sensor_driver.c
)

add_library(wash_core STATIC ${WASH_CORE_SOURCES})
target_include_directories(wash_core
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}
            ${PROJECT_SOURCE_DIR}/third_party/cjson)
if(NOT WIN32)
    target_link_libraries(wash_core PUBLIC m)
endif()

add_executable(wash_controller main/main.c)
target_include_directories(wash_controller PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(wash_controller PRIVATE wash_core)
