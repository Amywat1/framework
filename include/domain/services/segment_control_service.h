#ifndef DOMAIN_SERVICES_SEGMENT_CONTROL_SERVICE_H
#define DOMAIN_SERVICES_SEGMENT_CONTROL_SERVICE_H

#include <stdbool.h>

#include "domain/model/wash_execution.h"
#include "domain/model/wash_segment.h"
#include "domain/ports/sensor_port.h"
#include "shared/result_types.h"

/**
 * @file segment_control_service.h
 * @brief 定义段内持续控制与条件控制评估服务。
 */
typedef struct segment_control_evaluation_t {
    bool position_lost;
    bool follow_lost;
    bool segment_complete;
    bool start_chemical[MAX_SEGMENT_CONDITIONAL_CONTROLS];
    bool stop_chemical[MAX_SEGMENT_CONDITIONAL_CONTROLS];
} segment_control_evaluation_t;

operation_result_t segment_control_service_evaluate(const wash_segment_t *wash_segment,
    const wash_execution_t *wash_execution,
    const runtime_snapshot_t *runtime_snapshot,
    segment_control_evaluation_t *segment_control_evaluation);

#endif
