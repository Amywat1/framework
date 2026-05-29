#ifndef APPLICATION_SERVICES_ALARM_EVALUATOR_H
#define APPLICATION_SERVICES_ALARM_EVALUATOR_H

#include "domain/ports/sensor_port.h"

/**
 * @file alarm_evaluator.h
 * @brief 定义后台报警判定器接口。
 */

/**
 * @brief 后台报警判定器状态。
 *
 * @note 当前规则顺序固定为 `estop_active`、`!safety_ok`、`!resource_ok`、`!position_ok`。
 * @note 判定器只做报警上升沿检测：正常到报警时返回 `true`，报警持续期间不重复触发，
 *       报警恢复时只清内部 latch，不自动清业务故障。
 */
typedef struct alarm_evaluator_t
{
    bool alarm_active;
    bool pending_fault_present;
    const char *pending_fault_code;
    unsigned long pending_fault_occurred_at_ms;
} alarm_evaluator_t;

/**
 * @brief 初始化后台报警判定器。
 *
 * @param alarm_evaluator 判定器对象，不能为空。
 */
void alarm_evaluator_init(alarm_evaluator_t *alarm_evaluator);

/**
 * @brief 基于传感器快照执行一次报警边沿判定。
 *
 * @param alarm_evaluator 判定器对象，不能为空。
 * @param sensor_snapshot 本次采样快照，不能为空。
 * @param fault_code 输出报警码；当返回 `true` 时输出一个静态字符串指针。
 * @return 仅在“未报警 -> 报警”上升沿返回 `true`；报警持续期间不重复返回，
 *         报警恢复时只清内部 latch，不自动清业务故障。
 */
bool alarm_evaluator_evaluate(alarm_evaluator_t *alarm_evaluator,
                              const sensor_snapshot_t *sensor_snapshot,
                              unsigned long occurred_at_ms,
                              const char **fault_code,
                              unsigned long *fault_occurred_at_ms);

/**
 * @brief 确认当前告警已成功消费。
 *
 * @param alarm_evaluator 判定器对象，不能为空。
 * @note 仅在外部告警事件成功投递后调用，用于确认本轮持续告警已被消费。
 */
void alarm_evaluator_mark_delivered(alarm_evaluator_t *alarm_evaluator);

#endif
