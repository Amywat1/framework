#include "application/use_cases/query_wash_session_status.h"

#include <string.h>

#include "shared/error_codes.h"

operation_result_t query_wash_session_status_execute(system_context_t *system_context, wash_session_status_view_t *wash_session_status_view)
{
    if (system_context == 0 || wash_session_status_view == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    memset(wash_session_status_view, 0, sizeof(*wash_session_status_view));
    wash_session_status_view->has_active_session = (system_context->wash_session.session_state != SESSION_STATE_NONE);
    wash_session_status_view->session_state = system_context->wash_session.session_state;
    wash_session_status_view->execution_state = system_context->wash_execution.execution_state;
    strncpy(wash_session_status_view->session_id,
        system_context->wash_session.session_id,
        sizeof(wash_session_status_view->session_id) - 1);
    strncpy(wash_session_status_view->stage_id,
        system_context->wash_session.progress_stage_id,
        sizeof(wash_session_status_view->stage_id) - 1);
    strncpy(wash_session_status_view->reason_code,
        system_context->last_transition_record.reason_code,
        sizeof(wash_session_status_view->reason_code) - 1);
    return operation_result_ok();
}
