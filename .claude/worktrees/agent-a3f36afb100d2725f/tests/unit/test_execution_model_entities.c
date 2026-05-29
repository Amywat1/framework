#include "domain/model/conditional_control.h"
#include "domain/model/position_trigger.h"
#include "domain/model/wash_segment.h"
#include "tests/test_support.h"

static int test_valid_segment_entities(void)
{
    wash_segment_t wash_segment;

    wash_segment_init(&wash_segment, "roof_segment", "顶刷段", 1);
    wash_segment.segment_kind = SEGMENT_KIND_ROOF_BRUSH;
    wash_segment.motion_plan.direction = MOTION_DIRECTION_FORWARD;
    wash_segment.motion_plan.target_reference = MOTION_TARGET_TAIL;
    wash_segment.motion_plan.requires_position_feedback = true;
    wash_segment.completion_condition.trigger.reference = POSITION_REFERENCE_TAIL_REACHED;
    wash_segment.completion_condition.trigger.compare_mode = POSITION_COMPARE_TRUE;
    wash_segment.exit_actions.stop_roof_brush = true;
    wash_segment.exit_actions.stop_chemical = true;
    wash_segment.exit_actions.roof_brush_home = true;
    wash_segment.continuous_controls.roof_brush_follow = true;
    TEST_ASSERT(wash_segment_is_valid(&wash_segment));
    return 0;
}

static int test_invalid_conditional_basis_is_rejected(void)
{
    conditional_control_t conditional_control;

    conditional_control_init(&conditional_control);
    strncpy(conditional_control.control_id, "invalid", sizeof(conditional_control.control_id) - 1);
    conditional_control.kind = CONDITIONAL_CONTROL_CHEMICAL_WINDOW;
    conditional_control.control_object = ACTUATOR_CHEMICAL;
    conditional_control.basis = POSITION_REFERENCE_ABSOLUTE_MM;
    conditional_control.start_trigger.reference = POSITION_REFERENCE_ABSOLUTE_MM;
    conditional_control.start_trigger.compare_mode = POSITION_COMPARE_GREATER_EQUAL;
    conditional_control.stop_trigger.reference = POSITION_REFERENCE_DISTANCE_TO_HEAD_MM;
    conditional_control.stop_trigger.compare_mode = POSITION_COMPARE_LESS_EQUAL;
    TEST_ASSERT(!conditional_control_is_valid(&conditional_control));
    return 0;
}

int main(void)
{
    if (test_valid_segment_entities() != 0) {
        return 1;
    }
    if (test_invalid_conditional_basis_is_rejected() != 0) {
        return 1;
    }
    return 0;
}

