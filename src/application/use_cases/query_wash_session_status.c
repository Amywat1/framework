#include "application/use_cases/query_wash_session_status.h"

#include <string.h>

#include "platform/scheduler.h"
#include "shared/error_codes.h"
#include "src/application/coordinators/control_context_private.h"

operation_result_t query_wash_session_status_execute(wash_session_status_view_t *wash_session_status_view)
{
    const state_transition_record_t *last_transition_record;
    const wash_execution_t *wash_execution;
    const wash_session_t *wash_session;
    const wait_condition_t *wait_condition;

    if (wash_session_status_view == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    memset(wash_session_status_view, 0, sizeof(*wash_session_status_view));
    wash_session = control_context_private_wash_session();
    wash_execution = control_context_private_wash_execution();
    if (wash_session == 0 || wash_execution == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    wait_condition = control_context_wait_condition();
    last_transition_record = control_context_private_last_transition_record();

    wash_session_status_view->has_active_session =
        (wash_session->session_state == SESSION_STATE_CREATED || wash_session->session_state == SESSION_STATE_RUNNING);
    wash_session_status_view->device_state = control_context_private_device_state();
    wash_session_status_view->global_fault_present = control_context_private_global_fault_present();
    wash_session_status_view->session_state = wash_session->session_state;
    wash_session_status_view->execution_state = wash_execution->execution_state;
    wash_session_status_view->lifecycle_state = wash_execution->lifecycle_state;
    strncpy(wash_session_status_view->session_id, wash_session->session_id,
            sizeof(wash_session_status_view->session_id) - 1);
    strncpy(wash_session_status_view->stage_id, wash_session->progress_segment_id,
            sizeof(wash_session_status_view->stage_id) - 1);
    strncpy(wash_session_status_view->wait_condition_id, wait_condition->wait_condition_id,
            sizeof(wash_session_status_view->wait_condition_id) - 1);
    strncpy(wash_session_status_view->wait_reason, wait_condition->reason_code,
            sizeof(wash_session_status_view->wait_reason) - 1);
    {
        const char *last_reason = control_context_last_reason_code();
        strncpy(wash_session_status_view->reason_code,
                last_reason[0] != '\0' ? last_reason : last_transition_record->reason_code,
                sizeof(wash_session_status_view->reason_code) - 1);
    }
    strncpy(wash_session_status_view->global_fault_reason, control_context_private_global_fault_reason(),
            sizeof(wash_session_status_view->global_fault_reason) - 1);
    {
        scheduler_t *bound_scheduler = (scheduler_t *)control_context_bound_scheduler();
        if (scheduler_read_view(bound_scheduler, &wash_session_status_view->scheduler_view).ok)
        {
            wash_session_status_view->scheduler_view_available = true;
        }
    }
    return operation_result_ok();
}
