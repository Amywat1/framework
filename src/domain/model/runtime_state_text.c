#include "domain/model/runtime_state_text.h"

const char *runtime_state_text_device_state(device_state_t device_state)
{
    switch (device_state)
    {
    case DEVICE_STATE_INIT:
        return "init";
    case DEVICE_STATE_RECOVERING:
        return "recovering";
    case DEVICE_STATE_IDLE:
        return "idle";
    case DEVICE_STATE_RUNNING:
        return "running";
    case DEVICE_STATE_EXCEPTION:
        return "exception";
    case DEVICE_STATE_STOPPED:
    default:
        return "stopped";
    }
}

const char *runtime_state_text_session_state(session_state_t session_state)
{
    switch (session_state)
    {
    case SESSION_STATE_CREATED:
        return "created";
    case SESSION_STATE_RUNNING:
        return "running";
    case SESSION_STATE_COMPLETED:
        return "completed";
    case SESSION_STATE_ABORTED:
        return "aborted";
    default:
        return "none";
    }
}

const char *runtime_state_text_execution_state(execution_state_t execution_state)
{
    switch (execution_state)
    {
    case EXECUTION_STATE_RUNNING:
        return "running";
    case EXECUTION_STATE_COMPLETED:
        return "completed";
    case EXECUTION_STATE_ABORTED:
        return "aborted";
    default:
        return "none";
    }
}

const char *runtime_state_text_segment_lifecycle_state(segment_lifecycle_state_t lifecycle_state)
{
    switch (lifecycle_state)
    {
    case SEGMENT_LIFECYCLE_ENTERING:
        return "entering";
    case SEGMENT_LIFECYCLE_RUNNING:
        return "running";
    case SEGMENT_LIFECYCLE_EXITING:
        return "exiting";
    case SEGMENT_LIFECYCLE_COMPLETED:
        return "completed";
    case SEGMENT_LIFECYCLE_ABORTED:
        return "aborted";
    case SEGMENT_LIFECYCLE_PENDING:
    default:
        return "pending";
    }
}
