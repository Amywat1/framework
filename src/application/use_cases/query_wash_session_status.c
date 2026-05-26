#include "application/use_cases/query_wash_session_status.h"

#include <string.h>

#include "platform/scheduler.h"
#include "shared/error_codes.h"
#include "src/application/coordinators/device_runtime_private.h"

operation_result_t query_wash_session_status_execute(const device_runtime_t device_runtime,
                                                     wash_session_status_view_t *wash_session_status_view)
{
    const state_transition_record_t *last_transition_record;
    operation_result_t result;
    const wash_execution_t *wash_execution;
    const wash_session_t *wash_session;
    const wait_condition_t *wait_condition;

    if (wash_session_status_view == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    result = device_runtime_private_require_active(device_runtime);
    if (!result.ok)
    {
        return result;
    }
    memset(wash_session_status_view, 0, sizeof(*wash_session_status_view));
    wash_session = device_runtime_private_wash_session(device_runtime);
    wash_execution = device_runtime_private_wash_execution(device_runtime);
    wait_condition = device_runtime_private_wait_condition(device_runtime);
    last_transition_record = device_runtime_private_last_transition_record(device_runtime);
    if (wash_session == 0 || wash_execution == 0 || wait_condition == 0 || last_transition_record == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    wash_session_status_view->has_active_session =
        (wash_session->session_state == SESSION_STATE_CREATED || wash_session->session_state == SESSION_STATE_RUNNING);
    wash_session_status_view->device_state = device_runtime_private_device_state(device_runtime);
    wash_session_status_view->global_fault_present = device_runtime_private_global_fault_present(device_runtime);
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
    const char *last_reason = device_runtime_last_reason_code(device_runtime);
    strncpy(wash_session_status_view->reason_code,
            last_reason[0] != '\0' ? last_reason : last_transition_record->reason_code,
            sizeof(wash_session_status_view->reason_code) - 1);
    strncpy(wash_session_status_view->global_fault_reason, device_runtime_private_global_fault_reason(device_runtime),
            sizeof(wash_session_status_view->global_fault_reason) - 1);
    {
        scheduler_t *bound_scheduler =
            (scheduler_t *)device_runtime_bound_scheduler(device_runtime);
        if (scheduler_read_view(bound_scheduler, &wash_session_status_view->scheduler_view).ok)
        {
            wash_session_status_view->scheduler_view_available = true;
        }
    }
    return operation_result_ok();
}
