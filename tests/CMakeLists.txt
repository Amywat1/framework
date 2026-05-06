enable_testing()

function(add_wash_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE wash_core)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

add_wash_test(test_unit_execution_entities unit/test_execution_model_entities.c)
add_wash_test(test_unit_session_state_machine unit/test_wash_session_state_machine.c)
add_wash_test(test_unit_trigger_priority unit/test_trigger_priority_service.c)
add_wash_test(test_unit_wait_timeout unit/test_wait_timeout_service.c)
add_wash_test(test_unit_program_snapshot unit/test_program_snapshot_service.c)

add_wash_test(test_contract_session_command contract/test_session_command_contract.c)
add_wash_test(test_contract_execution_events contract/test_execution_events_contract.c)
add_wash_test(test_contract_program_snapshot contract/test_program_snapshot_contract.c)

add_wash_test(test_integration_progression integration/test_event_driven_session_progression.c)
add_wash_test(test_integration_exception_paths integration/test_session_exception_paths.c)
add_wash_test(test_integration_snapshot_consistency integration/test_program_snapshot_consistency.c)
add_wash_test(test_integration_status integration/test_session_status_observability.c)

add_wash_test(test_sim_progression simulation/test_event_driven_progression.c)
add_wash_test(test_sim_priority simulation/test_trigger_priority_competition.c)
add_wash_test(test_sim_snapshot_runtime simulation/test_program_snapshot_runtime_behavior.c)
