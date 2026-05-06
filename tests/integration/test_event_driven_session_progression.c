#include "tests/test_support.h"

#include "domain/model/chemical_action.h"
#include "domain/model/program_snapshot.h"
#include "domain/model/wash_program.h"
#include "domain/model/wash_execution.h"
#include "domain/model/wash_session.h"
#include "domain/model/wash_stage.h"
#include "domain/services/wash_execution_service.h"

static int verify_standard_progression(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_execution.execution_state == EXECUTION_STATE_RUNNING);
    TEST_ASSERT(driver_context.move_count == 1);
    TEST_ASSERT(driver_context.chemical_count == 1);

    result = test_submit_feedback(&system_context, "feedback:pre_soak", "progress-1");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.move_count == 2);
    TEST_ASSERT(driver_context.chemical_count == 1);

    result = test_submit_feedback(&system_context, "feedback:wash", "progress-2");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_COMPLETED);
    TEST_ASSERT(driver_context.roof_brush_count == 2);
    TEST_ASSERT(driver_context.side_brush_count == 2);
    return 0;
}

static int verify_multiple_chemical_actions_dispatched(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_program_t wash_program;
    wash_stage_t wash_stage;
    chemical_action_t foam_action;
    chemical_action_t wax_action;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    wash_program_init(&wash_program, "multi_chemical", "multi_chemical");
    wash_stage_init(&wash_stage, "pre_soak", "pre_soak", 1);
    chemical_action_init(&foam_action, "foam", 1000);
    chemical_action_init(&wax_action, "wax", 800);
    wax_action.retry_limit = 2;
    TEST_ASSERT(wash_stage_add_chemical_action(&wash_stage, &foam_action) == 0);
    TEST_ASSERT(wash_stage_add_chemical_action(&wash_stage, &wax_action) == 0);
    TEST_ASSERT(wash_program_add_stage(&wash_program, &wash_stage) == 0);
    program_snapshot_capture(&system_context.program_snapshot,
        wash_program.program_id,
        wash_program.revision,
        1,
        &wash_program,
        PROGRAM_SNAPSHOT_VALIDATION_VALID);
    wash_session_create(&system_context.wash_session,
        wash_program.program_id,
        system_context.program_snapshot.program_snapshot_id,
        system_context.current_time_ms,
        1);
    wash_session_start_running(&system_context.wash_session);
    wash_execution_reset(&system_context.wash_execution);
    result = wash_execution_service_begin_next_stage(&system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.chemical_count == 2);
    return 0;
}

int main(void)
{
    if (verify_standard_progression() != 0) {
        return 1;
    }
    if (verify_multiple_chemical_actions_dispatched() != 0) {
        return 1;
    }
    return 0;
}
