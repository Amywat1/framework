set(WASH_CORE_SOURCES
    domain/model/wash_program.c
    domain/model/wash_stage.c
    domain/model/vehicle_type.c
    domain/model/chemical_channel.c
    domain/model/chemical_action.c
    domain/model/fault_event.c
    domain/model/safety_interlock.c
    domain/model/program_validation.c
    domain/model/program_snapshot.c
    domain/model/state_transition_record.c
    domain/model/wait_condition.c
    domain/model/wash_execution.c
    domain/model/wash_session.c
    domain/model/wash_trigger_event.c
    domain/services/precheck_service.c
    domain/services/gantry_motion_state_machine.c
    domain/services/brush_control_state_machine.c
    domain/services/chemical_dosing_state_machine.c
    domain/services/fault_policy_service.c
    domain/services/program_snapshot_service.c
    domain/services/recovery_state_machine.c
    domain/services/trigger_priority_service.c
    domain/services/wait_timeout_service.c
    domain/services/wash_execution_service.c
    domain/services/wash_session_state_machine.c
    application/coordinators/compatibility_trigger_runner.c
    application/coordinators/runtime_event_recorder.c
    application/use_cases/process_wash_trigger.c
    application/use_cases/query_wash_session_status.c
    application/use_cases/start_wash_cycle.c
    application/use_cases/stop_wash_cycle.c
    application/use_cases/acknowledge_fault.c
    adapters/inbound/cli_command_adapter.c
    adapters/outbound/file_program_repository.c
    adapters/config/json_program_parser.c
    adapters/logging/file_event_logger.c
    platform/linux/main_loop.c
    platform/drivers/simulated_gantry_driver.c
    platform/drivers/simulated_brush_driver.c
    platform/drivers/simulated_chemical_driver.c
    platform/drivers/simulated_sensor_driver.c
)

add_library(wash_core STATIC ${WASH_CORE_SOURCES})
target_include_directories(wash_core PUBLIC ${PROJECT_SOURCE_DIR}/include)

add_executable(wash_controller main/main.c)
target_link_libraries(wash_controller PRIVATE wash_core)
