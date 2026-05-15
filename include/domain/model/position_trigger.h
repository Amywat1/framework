#ifndef DOMAIN_MODEL_POSITION_TRIGGER_H
#define DOMAIN_MODEL_POSITION_TRIGGER_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"

/**
 * @file position_trigger.h
 * @brief 定义统一位置快照与位置触发器。
 */
typedef struct position_snapshot_t
{
    bool position_valid;
    int gantry_absolute_mm;
    int distance_to_vehicle_head_mm;
    int distance_to_vehicle_tail_mm;
    bool home_reached;
    bool head_reached;
    bool tail_reached;
} position_snapshot_t;

typedef struct position_trigger_t
{
    position_reference_t reference;
    position_compare_mode_t compare_mode;
    int value_mm;
    int upper_value_mm;
    int tolerance_mm;
} position_trigger_t;

void position_trigger_init(position_trigger_t *position_trigger);
bool position_trigger_is_valid(const position_trigger_t *position_trigger);
bool position_trigger_matches(const position_trigger_t *position_trigger, const position_snapshot_t *position_snapshot);
int position_trigger_measure(const position_trigger_t *position_trigger, const position_snapshot_t *position_snapshot);

#endif
