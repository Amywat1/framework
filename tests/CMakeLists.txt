enable_testing()

function(add_wash_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE wash_core)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

# Unit
add_wash_test(test_unit_execution_entities unit/test_execution_model_entities.c)
add_wash_test(test_unit_program_config_validation unit/test_program_config_validation.c)

# Contract
add_wash_test(test_contract_program_config_adapter contract/test_program_config_adapter_contract.c)
add_wash_test(test_contract_segment_runtime contract/test_segment_runtime_orchestration_contract.c)
add_wash_test(test_contract_position_exit contract/test_position_exit_semantics_contract.c)

# Integration
add_wash_test(test_integration_segment_runtime integration/test_segment_runtime_orchestration.c)
add_wash_test(test_integration_controller_runtime integration/test_controller_runtime_persistence.c)
add_wash_test(test_integration_segment_exception_paths integration/test_segment_exception_paths.c)
add_wash_test(test_integration_session_status_observability integration/test_session_status_observability.c)

# Simulation
add_wash_test(test_sim_basic_control_loop simulation/test_basic_control_loop.c)
add_wash_test(test_sim_controller_global_fault_behavior simulation/test_controller_global_fault_behavior.c)
add_wash_test(test_sim_segment_runtime_control simulation/test_segment_runtime_control.c)
add_wash_test(test_sim_segment_exception_behavior simulation/test_segment_exception_behavior.c)
