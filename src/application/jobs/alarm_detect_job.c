#include "src/application/jobs/alarm_detect_job.h"

#include <string.h>

#include "src/application/coordinators/device_runtime_private.h"

#define BACKGROUND_ALARM_CORRELATION_KEY "background-alarm-monitor"
#define BACKGROUND_ALARM_SOURCE "background-alarm-monitor"

operation_result_t alarm_detect_job_process_snapshot(alarm_evaluator_t *alarm_evaluator,
                                                     const sensor_snapshot_t *sensor_snapshot,
                                                     unsigned long occurred_at_ms)
{
    const char *fault_code;
    unsigned long fault_occurred_at_ms;
    operation_result_t result;
    wash_trigger_event_t wash_trigger_event;

    fault_code = 0;
    fault_occurred_at_ms = 0ul;
    if (alarm_evaluator == 0 || sensor_snapshot == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (!alarm_evaluator_evaluate(alarm_evaluator, sensor_snapshot, occurred_at_ms, &fault_code, &fault_occurred_at_ms))
    {
        return operation_result_ok();
    }

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_FAULT, 0, fault_code, BACKGROUND_ALARM_CORRELATION_KEY,
                            fault_occurred_at_ms);
    strncpy(wash_trigger_event.source, BACKGROUND_ALARM_SOURCE, sizeof(wash_trigger_event.source) - 1u);
    result = device_runtime_private_enqueue_external_trigger(&wash_trigger_event);
    if (!result.ok)
    {
        return result;
    }

    alarm_evaluator_mark_delivered(alarm_evaluator);
    return result;
}
