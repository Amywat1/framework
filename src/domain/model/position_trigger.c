#include "domain/model/position_trigger.h"

#include <string.h>

void position_trigger_init(position_trigger_t *position_trigger)
{
    if (position_trigger == 0) {
        return;
    }

    memset(position_trigger, 0, sizeof(*position_trigger));
}

static bool reference_is_boolean(position_reference_t reference)
{
    return reference == POSITION_REFERENCE_HEAD_REACHED
        || reference == POSITION_REFERENCE_TAIL_REACHED
        || reference == POSITION_REFERENCE_HOME_REACHED;
}

bool position_trigger_is_valid(const position_trigger_t *position_trigger)
{
    if (position_trigger == 0 || position_trigger->reference == POSITION_REFERENCE_NONE) {
        return false;
    }
    if (reference_is_boolean(position_trigger->reference)) {
        return position_trigger->compare_mode == POSITION_COMPARE_TRUE;
    }
    if (position_trigger->compare_mode == POSITION_COMPARE_RANGE_INCLUSIVE) {
        return position_trigger->upper_value_mm >= position_trigger->value_mm;
    }
    return true;
}

int position_trigger_measure(const position_trigger_t *position_trigger, const position_snapshot_t *position_snapshot)
{
    if (position_trigger == 0 || position_snapshot == 0) {
        return 0;
    }

    switch (position_trigger->reference) {
        case POSITION_REFERENCE_ABSOLUTE_MM:
            return position_snapshot->gantry_absolute_mm;
        case POSITION_REFERENCE_DISTANCE_TO_HEAD_MM:
            return position_snapshot->distance_to_vehicle_head_mm;
        case POSITION_REFERENCE_DISTANCE_TO_TAIL_MM:
            return position_snapshot->distance_to_vehicle_tail_mm;
        case POSITION_REFERENCE_HEAD_REACHED:
            return position_snapshot->head_reached ? 1 : 0;
        case POSITION_REFERENCE_TAIL_REACHED:
            return position_snapshot->tail_reached ? 1 : 0;
        case POSITION_REFERENCE_HOME_REACHED:
            return position_snapshot->home_reached ? 1 : 0;
        case POSITION_REFERENCE_NONE:
        default:
            return 0;
    }
}

bool position_trigger_matches(const position_trigger_t *position_trigger, const position_snapshot_t *position_snapshot)
{
    int value;

    if (!position_trigger_is_valid(position_trigger) || position_snapshot == 0) {
        return false;
    }
    if (!reference_is_boolean(position_trigger->reference) && !position_snapshot->position_valid) {
        return false;
    }

    value = position_trigger_measure(position_trigger, position_snapshot);
    switch (position_trigger->compare_mode) {
        case POSITION_COMPARE_TRUE:
            return value != 0;
        case POSITION_COMPARE_GREATER_EQUAL:
            return value + position_trigger->tolerance_mm >= position_trigger->value_mm;
        case POSITION_COMPARE_LESS_EQUAL:
            return value - position_trigger->tolerance_mm <= position_trigger->value_mm;
        case POSITION_COMPARE_RANGE_INCLUSIVE:
            return value >= (position_trigger->value_mm - position_trigger->tolerance_mm)
                && value <= (position_trigger->upper_value_mm + position_trigger->tolerance_mm);
        default:
            return false;
    }
}
