#include "application/use_cases/process_formal_command.h"
#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

static int verify_formal_start_rejects_unavailable_program_from_idle(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    char response_line[512];
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    result = process_formal_command_execute(system_context,
        "start missing_program",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "detail=queued") != 0);

    result = test_flush_pending_runtime(system_context);
    TEST_ASSERT(!result.ok);
    test_rebuild_formal_response_line(system_context, response_line, sizeof(response_line));
    TEST_ASSERT(strstr(response_line, "result=rejected accepted=false detail=program_unavailable") != 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "program_unavailable") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_START);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "program_unavailable") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    test_release_system_context(system_context);
    return 0;
}

static int verify_formal_start_rejects_invalid_program_from_idle(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    char response_line[512];
    operation_result_t result;
    wash_program_t invalid_program;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        &invalid_program);
    TEST_ASSERT(result.ok);
    strncpy(invalid_program.program_id, "invalid_runtime_program", sizeof(invalid_program.program_id) - 1);
    invalid_program.program_id[sizeof(invalid_program.program_id) - 1] = '\0';
    invalid_program.segments[0].sequence_no = 2;
    file_program_repository_set_runtime_program(system_context, &invalid_program, invalid_program.revision);
    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    result = process_formal_command_execute(system_context,
        "start invalid_runtime_program",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "detail=queued") != 0);

    result = test_flush_pending_runtime(system_context);
    TEST_ASSERT(!result.ok);
    test_rebuild_formal_response_line(system_context, response_line, sizeof(response_line));
    TEST_ASSERT(strstr(response_line, "result=rejected accepted=false detail=program_invalid") != 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "program_invalid") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_START);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "program_invalid") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    test_release_system_context(system_context);
    return 0;
}

static int verify_formal_start_rejects_when_global_fault_active_from_idle(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    char response_line[512];
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);

    system_context_private_set_global_fault(system_context, "E_STOP", "forced-global-fault");
    system_context_private_set_device_state(system_context, DEVICE_STATE_IDLE);

    result = process_formal_command_execute(system_context,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "detail=queued") != 0);

    result = test_flush_pending_runtime(system_context);
    TEST_ASSERT(!result.ok);
    test_rebuild_formal_response_line(system_context, response_line, sizeof(response_line));
    TEST_ASSERT(strstr(response_line, "result=rejected accepted=false detail=global_fault_active") != 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "global_fault_active") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_START);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "global_fault_active") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);
    TEST_ASSERT(system_context_private_runtime(system_context)->global_fault_present);

    test_release_system_context(system_context);
    return 0;
}

static int verify_formal_start_rejects_when_running_session_exists(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    char response_line[512];
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_RUNNING);

    system_context_private_set_device_state(system_context, DEVICE_STATE_IDLE);

    result = process_formal_command_execute(system_context,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "result=invalid_state accepted=false detail=running_session_exists") != 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "running_session_exists") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_START);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_transition_record.reason_code, "running_session_exists") == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_RUNNING);

    test_release_system_context(system_context);
    return 0;
}

static int verify_formal_command_execute_is_public_entry(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    char response_line[512];
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);

    result = process_formal_command_execute(system_context,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "detail=queued") != 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->pending_trigger_count == 1u);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state != SESSION_STATE_RUNNING);
    result = test_flush_pending_runtime(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->pending_trigger_count == 0u);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_reason_code[0] != '\0');
    test_release_system_context(system_context);
    return 0;
}

static int verify_cli_execute_uses_formal_entry(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    char response_line[512];
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);

    result = process_formal_command_execute(system_context,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "detail=queued") != 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->pending_trigger_count == 1u);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state != SESSION_STATE_RUNNING);
    result = test_flush_pending_runtime(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->pending_trigger_count == 0u);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(system_context_private_runtime(system_context)->last_reason_code[0] != '\0');
    test_release_system_context(system_context);
    return 0;
}

int main(void)
{
    if (verify_formal_start_rejects_unavailable_program_from_idle() != 0) {
        return 1;
    }
    if (verify_formal_start_rejects_invalid_program_from_idle() != 0) {
        return 1;
    }
    if (verify_formal_start_rejects_when_global_fault_active_from_idle() != 0) {
        return 1;
    }
    if (verify_formal_start_rejects_when_running_session_exists() != 0) {
        return 1;
    }
    if (verify_formal_command_execute_is_public_entry() != 0) {
        return 1;
    }
    if (verify_cli_execute_uses_formal_entry() != 0) {
        return 1;
    }
    return 0;
}

