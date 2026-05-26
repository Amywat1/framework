#ifndef APPLICATION_USE_CASES_QUERY_WASH_SESSION_STATUS_H
#define APPLICATION_USE_CASES_QUERY_WASH_SESSION_STATUS_H

#include <stdbool.h>

#include "application/coordinators/device_runtime.h"
#include "domain/model/domain_enums.h"
#include "platform/scheduler.h"
#include "shared/result_types.h"

typedef struct wash_session_status_view_t
{
    bool has_active_session;
    bool global_fault_present;
    device_state_t device_state;
    char session_id[32];
    session_state_t session_state;
    execution_state_t execution_state;
    segment_lifecycle_state_t lifecycle_state;
    char stage_id[32];
    char wait_condition_id[32];
    char wait_reason[64];
    char reason_code[64];
    char global_fault_reason[128];
    bool scheduler_view_available;
    scheduler_state_view_t scheduler_view;
} wash_session_status_view_t;

operation_result_t query_wash_session_status_execute(const device_runtime_t device_runtime,
                                                     wash_session_status_view_t *wash_session_status_view);

#endif
