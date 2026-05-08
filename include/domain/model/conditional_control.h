#ifndef DOMAIN_MODEL_CONDITIONAL_CONTROL_H
#define DOMAIN_MODEL_CONDITIONAL_CONTROL_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"
#include "domain/model/position_trigger.h"

#define MAX_SEGMENT_CONDITIONAL_CONTROLS (4)

/**
 * @file conditional_control.h
 * @brief 定义受限条件控制模型。
 */
typedef struct conditional_control_t {
    char control_id[32];
    conditional_control_kind_t kind;
    actuator_category_t control_object;
    position_reference_t basis;
    position_trigger_t start_trigger;
    position_trigger_t stop_trigger;
    bool active_during_exit;
} conditional_control_t;

void conditional_control_init(conditional_control_t *conditional_control);
bool conditional_control_is_valid(const conditional_control_t *conditional_control);

#endif
