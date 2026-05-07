#include "tests/test_support.h"

#include "domain/model/chemical_action.h"
#include "domain/model/program_snapshot.h"
#include "domain/model/wash_program.h"
#include "domain/model/wash_stage.h"

static int setup_single_stage_runtime(system_context_t *system_context)
{
    wash_program_t wash_program;
    wash_stage_t wash_stage;
    chemical_action_t chemical_action;

    wash_program_init(&wash_program, "single_stage", "single_stage");
    wash_stage_init(&wash_stage, "pre_soak", "pre_soak", 1);
    chemical_action_init(&chemical_action, "foam", 1000);
    TEST_ASSERT(wash_stage_add_chemical_action(&wash_stage, &chemical_action) == 0);
    TEST_ASSERT(wash_program_add_stage(&wash_program, &wash_stage) == 0);
    program_snapshot_capture(&system_context->program_snapshot,
        wash_program.program_id,
        wash_program.revision,
        1,
        &wash_program,
        PROGRAM_SNAPSHOT_VALIDATION_VALID);
    wash_session_create(&system_context->wash_session,
        wash_program.program_id,
        system_context->program_snapshot.program_snapshot_id,
        system_context->current_time_ms,
        1);
    wash_session_start_running(&system_context->wash_session);
    wash_execution_reset(&system_context->wash_execution);
    return 0;
}

static int verify_begin_next_stage_returns_waiting_fact(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    if (setup_single_stage_runtime(&system_context) != 0) {
        return 1;
    }

    wash_execution_service_args = test_build_wash_execution_service_args(&system_context);
    result = wash_execution_service_begin_next_stage(&wash_execution_service_args, &wash_execution_fact);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_execution.execution_state == EXECUTION_STATE_RUNNING);
    TEST_ASSERT(system_context.wait_condition.active == true);
    TEST_ASSERT(strcmp(wash_execution_fact.current_state, "running") == 0);
    TEST_ASSERT(strcmp(wash_execution_fact.result_code, "waiting") == 0);
    TEST_ASSERT(strcmp(wash_execution_fact.stage_id, "pre_soak") == 0);
    return 0;
}

static int verify_feedback_returns_advanced_fact(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    if (setup_single_stage_runtime(&system_context) != 0) {
        return 1;
    }

    wash_execution_service_args = test_build_wash_execution_service_args(&system_context);
    result = wash_execution_service_begin_next_stage(&wash_execution_service_args, &wash_execution_fact);
    TEST_ASSERT(result.ok);

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_DEVICE_FEEDBACK,
        0,
        "feedback:pre_soak",
        "unit-feedback",
        system_context.current_time_ms);
    wash_execution_service_args.current_time_ms = 100;
    result = wash_execution_service_handle_feedback(&wash_execution_service_args,
        &wash_trigger_event,
        &wash_execution_fact);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_execution.execution_state == EXECUTION_STATE_COMPLETED);
    TEST_ASSERT(strcmp(wash_execution_fact.result_code, "advanced") == 0);
    TEST_ASSERT(strcmp(wash_execution_fact.reason_code, "pre_soak") == 0);
    return 0;
}

static int verify_stop_returns_stopped_fact(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    if (setup_single_stage_runtime(&system_context) != 0) {
        return 1;
    }

    wash_execution_service_args = test_build_wash_execution_service_args(&system_context);
    result = wash_execution_service_begin_next_stage(&wash_execution_service_args, &wash_execution_fact);
    TEST_ASSERT(result.ok);

    wash_execution_service_args.current_time_ms = 200;
    result = wash_execution_service_handle_stop(&wash_execution_service_args, "manual-stop", &wash_execution_fact);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_execution.execution_state == EXECUTION_STATE_ABORTED);
    TEST_ASSERT(strcmp(wash_execution_fact.result_code, "stopped") == 0);
    TEST_ASSERT(strcmp(wash_execution_fact.reason_code, "manual-stop") == 0);
    return 0;
}

int main(void)
{
    if (verify_begin_next_stage_returns_waiting_fact() != 0) {
        return 1;
    }
    if (verify_feedback_returns_advanced_fact() != 0) {
        return 1;
    }
    if (verify_stop_returns_stopped_fact() != 0) {
        return 1;
    }
    return 0;
}
