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
    domain/model/wash_state_text.c
    domain/model/state_transition_record.c
    domain/model/wait_condition.c
    domain/model/wash_execution.c
    domain/model/wash_session.c
    domain/model/wash_trigger_event.c
    domain/services/fault_policy_service.c
    application/services/alarm_evaluator.c
    application/services/command_dispatch.c
    domain/services/program_snapshot_service.c
    domain/services/recovery_state_machine.c
    domain/services/segment_control_service.c
    domain/services/trigger_priority_service.c
    domain/services/wait_timeout_service.c
    domain/services/wash_execution_service.c
    domain/services/wash_session_state_machine.c
    startup/app_bootstrap.c
    application/coordinators/alarm_monitor.c
    application/coordinators/control_tick.c
    application/coordinators/control_context.c
    application/coordinators/control_outcome_recorder.c
    application/coordinators/scheduler_runtime_port.c
    application/jobs/alarm_detect_job.c
    application/use_cases/formal_command.c
    application/use_cases/wash_control.c
    application/use_cases/query_wash_session_status.c
    adapters/outbound/file_program_repository.c
    adapters/config/json_program_parser.c
    platform/linux/scheduler_linux.c
    platform/linux/command_ingress_stdio_linux.c
    platform/linux/worker_thread_linux.c
    platform/drivers/simulated_driver_context.c
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
find_package(Threads REQUIRED)
if(NOT WIN32)
    target_link_libraries(wash_core PUBLIC m Threads::Threads)
else()
    target_link_libraries(wash_core PUBLIC Threads::Threads)
endif()

add_executable(wash_controller startup/main.c)
target_include_directories(wash_controller PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(wash_controller PRIVATE wash_core)
