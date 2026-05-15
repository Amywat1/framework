#include "domain/services/segment_control_service.h"

#include <string.h>

#include "shared/error_codes.h"

operation_result_t segment_control_service_evaluate(const wash_segment_t *wash_segment,
                                                    const wash_execution_t *wash_execution,
                                                    const runtime_snapshot_t *runtime_snapshot,
                                                    segment_control_evaluation_t *segment_control_evaluation)
{
    int index;

    if (wash_segment == 0 || wash_execution == 0 || runtime_snapshot == 0 || segment_control_evaluation == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    memset(segment_control_evaluation, 0, sizeof(*segment_control_evaluation));
    if (wash_segment->motion_plan.requires_position_feedback && !runtime_snapshot->position_snapshot.position_valid)
    {
        segment_control_evaluation->position_lost = true;
        return operation_result_ok();
    }
    if (wash_segment->continuous_controls.roof_brush_follow &&
        !runtime_snapshot->actuator_feedback.roof_brush_follow_ok)
    {
        segment_control_evaluation->follow_lost = true;
    }

    for (index = 0; index < wash_segment->conditional_control_count; ++index)
    {
        const conditional_control_t *conditional_control = &wash_segment->conditional_controls[index];
        bool active = wash_execution->active_conditional_controls[index];

        if (!active &&
            position_trigger_matches(&conditional_control->start_trigger, &runtime_snapshot->position_snapshot))
        {
            segment_control_evaluation->start_chemical[index] = true;
        }
        if (active &&
            position_trigger_matches(&conditional_control->stop_trigger, &runtime_snapshot->position_snapshot))
        {
            segment_control_evaluation->stop_chemical[index] = true;
        }
    }

    segment_control_evaluation->segment_complete =
        position_trigger_matches(&wash_segment->completion_condition.trigger, &runtime_snapshot->position_snapshot);
    return operation_result_ok();
}
