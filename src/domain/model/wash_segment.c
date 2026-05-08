#include "domain/model/wash_segment.h"

#include <string.h>

#include "shared/timeouts.h"

void wash_segment_init(wash_segment_t *wash_segment, const char *segment_id, const char *segment_name, int sequence_no)
{
    if (wash_segment == 0) {
        return;
    }

    memset(wash_segment, 0, sizeof(*wash_segment));
    if (segment_id != 0) {
        strncpy(wash_segment->segment_id, segment_id, sizeof(wash_segment->segment_id) - 1);
    }
    if (segment_name != 0) {
        strncpy(wash_segment->segment_name, segment_name, sizeof(wash_segment->segment_name) - 1);
    }
    wash_segment->sequence_no = sequence_no;
    wash_segment->segment_timeout_ms = DEFAULT_SEGMENT_TIMEOUT_MS;
    wash_segment->exit_timeout_ms = DEFAULT_EXIT_TIMEOUT_MS;
    segment_motion_plan_init(&wash_segment->motion_plan);
    position_trigger_init(&wash_segment->completion_condition.trigger);
    segment_exit_actions_init(&wash_segment->exit_actions);
    segment_exception_policy_init(&wash_segment->exception_policy);
}

bool wash_segment_is_valid(const wash_segment_t *wash_segment)
{
    int index;

    if (wash_segment == 0 || wash_segment->segment_id[0] == '\0') {
        return false;
    }
    if (!segment_motion_plan_is_valid(&wash_segment->motion_plan)) {
        return false;
    }
    if (!position_trigger_is_valid(&wash_segment->completion_condition.trigger)) {
        return false;
    }
    for (index = 0; index < wash_segment->conditional_control_count; ++index) {
        if (!conditional_control_is_valid(&wash_segment->conditional_controls[index])) {
            return false;
        }
    }
    return true;
}
