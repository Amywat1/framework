#include "domain/model/position_trigger.h"
#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

static int test_position_trigger_explains_start_and_stop(void)
{
    position_trigger_t position_trigger;
    position_snapshot_t position_snapshot;

    position_trigger_init(&position_trigger);
    position_trigger.reference = POSITION_REFERENCE_ABSOLUTE_MM;
    position_trigger.compare_mode = POSITION_COMPARE_GREATER_EQUAL;
    position_trigger.value_mm = 3000;
    memset(&position_snapshot, 0, sizeof(position_snapshot));
    position_snapshot.position_valid = true;
    position_snapshot.gantry_absolute_mm = 2500;
    TEST_ASSERT(!position_trigger_matches(&position_trigger, &position_snapshot));
    position_snapshot.gantry_absolute_mm = 3200;
    TEST_ASSERT(position_trigger_matches(&position_trigger, &position_snapshot));
    return 0;
}

static int test_target_reached_is_not_exit_complete(void)
{
    operation_result_t result;
    simulated_driver_context_t driver_context;

    test_setup_control_context( &driver_context);
    result = test_load_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush( "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 9500;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    result = test_tick( 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_wash_execution()->lifecycle_state == SEGMENT_LIFECYCLE_EXITING);
    TEST_ASSERT(control_context_private_wash_session()->session_state == SESSION_STATE_RUNNING);
    test_release_control_context();
    return 0;
}

int main(void)
{
    if (test_position_trigger_explains_start_and_stop() != 0) {
        return 1;
    }
    if (test_target_reached_is_not_exit_complete() != 0) {
        return 1;
    }
    return 0;
}

