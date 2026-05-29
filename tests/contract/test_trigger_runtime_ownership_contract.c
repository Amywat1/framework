#include "application/coordinators/runtime_event_recorder.h"
#include "application/coordinators/runtime_result_projection.h"
#include "application/use_cases/process_wash_trigger.h"
#include "application/coordinators/control_tick.h"
#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

static int verify_main_loop_owns_queue_and_time(void)
{
    simulated_driver_context_t driver_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    TEST_ASSERT(control_context_pending_trigger_count() == 0u);
    TEST_ASSERT(control_context_current_time_ms() == 0ul);

    control_tick_advance_time( 25ul);
    TEST_ASSERT(control_context_current_time_ms() == 25ul);

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_STOP,
        0,
        "ownership-stop",
        "contract",
        control_context_current_time_ms());
    result = control_tick_submit_trigger( &wash_trigger_event);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_pending_trigger_count() == 1u);
    TEST_ASSERT(control_context_private_global_fault_present() == false);
    TEST_ASSERT(control_context_last_result_code()[0] == '\0');
    test_release_system_context();
    return 0;
}

static int verify_recorder_owns_latest_projection(void)
{
    simulated_driver_context_t driver_context;
    runtime_result_projection_t runtime_result_projection;

    test_setup_system_context( &driver_context);
    wash_session_abort(control_context_private_wash_session_mutable(), RESULT_CODE_MANUAL_ABORT, "already-final", 77ul);

    runtime_result_projection_init(&runtime_result_projection);
    runtime_result_projection_set_latest_result(&runtime_result_projection, "accepted", "projection-only");
    runtime_result_projection_set_transition(&runtime_result_projection,
        TRANSITION_ENTITY_REQUEST,
        "formal-command",
        TRIGGER_TYPE_START,
        "received",
        "queued",
        "accepted",
        "projection-only");
    runtime_event_recorder_apply_projection( &runtime_result_projection);

    TEST_ASSERT(strcmp(control_context_last_result_code(), "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_last_reason_code(), "projection-only") == 0);
    TEST_ASSERT(strcmp(control_context_private_last_transition_record()->reason_code, "projection-only") == 0);
    TEST_ASSERT(control_context_private_wash_session()->final_session_result == RESULT_CODE_MANUAL_ABORT);
    test_release_system_context();
    return 0;
}

static int verify_trigger_entry_owns_global_fault(void)
{
    simulated_driver_context_t driver_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_FAULT,
        0,
        "E_STOP",
        "ownership-fault",
        0ul);
    result = process_wash_trigger_execute( &wash_trigger_event);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_global_fault_present());
    TEST_ASSERT(strcmp(control_context_private_global_fault_code(), "E_STOP") == 0);
    TEST_ASSERT(strcmp(control_context_private_global_fault_reason(), "ownership-fault") == 0);
    TEST_ASSERT(strcmp(control_context_last_result_code(), "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_last_reason_code(), "global_fault_recorded") == 0);
    test_release_system_context();
    return 0;
}

static int verify_released_handle_cannot_reenter_runtime_ownership_paths(void)
{
    simulated_driver_context_t driver_context;
    wash_trigger_event_t wash_trigger_event;
    runtime_result_projection_t runtime_result_projection;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    test_release_system_context();

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_STOP,
        0,
        "released",
        "released",
        0ul);
    result = control_tick_submit_trigger( &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = process_wash_trigger_execute( &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    runtime_result_projection_init(&runtime_result_projection);
    runtime_result_projection_set_latest_result(&runtime_result_projection, "accepted", "released-projection");
    runtime_event_recorder_apply_projection( &runtime_result_projection);
    return 0;
}

int main(void)
{
    if (verify_main_loop_owns_queue_and_time() != 0) {
        return 1;
    }
    if (verify_recorder_owns_latest_projection() != 0) {
        return 1;
    }
    if (verify_trigger_entry_owns_global_fault() != 0) {
        return 1;
    }
    if (verify_released_handle_cannot_reenter_runtime_ownership_paths() != 0) {
        return 1;
    }
    return 0;
}

