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
/**
 * @brief 描述一次段内控制评估得到的结论。
 */
typedef struct segment_control_evaluation_t
{
    /**< 是否判定为位置丢失。 */
    bool position_lost;
    /**< 是否判定为跟随丢失。 */
    bool follow_lost;
    /**< 当前工步段是否已完成。 */
    bool segment_complete;
    /**< 各条件控制是否需要启动化学剂。 */
    bool start_chemical[MAX_SEGMENT_CONDITIONAL_CONTROLS];
    /**< 各条件控制是否需要停止化学剂。 */
    bool stop_chemical[MAX_SEGMENT_CONDITIONAL_CONTROLS];
} segment_control_evaluation_t;

/**
 * @brief 评估当前工步段的持续控制与条件控制状态。
 * @param wash_segment 当前工步段定义。
 * @param wash_execution 当前执行状态。
 * @param runtime_snapshot 当前运行时快照。
 * @param segment_control_evaluation 输出评估结果。
 * @return 成功返回 `operation_result_ok()`，参数非法时返回失败结果。
 */
operation_result_t segment_control_service_evaluate(const wash_segment_t *wash_segment,
                                                    const wash_execution_t *wash_execution,
                                                    const runtime_snapshot_t *runtime_snapshot,
                                                    segment_control_evaluation_t *segment_control_evaluation);

#endif
