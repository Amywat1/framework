#include "application/services/alarm_evaluator.h"

#include <stddef.h>

typedef bool (*alarm_rule_detector_t)(const sensor_snapshot_t *sensor_snapshot);

typedef struct alarm_rule_t
{
    const char *fault_code;
    alarm_rule_detector_t detector;
} alarm_rule_t;

/** @brief 检测急停报警。 */
static bool detect_estop_active(const sensor_snapshot_t *sensor_snapshot)
{
    return sensor_snapshot != 0 && sensor_snapshot->estop_active;
}

/** @brief 检测安全联锁报警。 */
static bool detect_safety_not_ok(const sensor_snapshot_t *sensor_snapshot)
{
    return sensor_snapshot != 0 && !sensor_snapshot->safety_ok;
}

/** @brief 检测资源就绪报警。 */
static bool detect_resource_not_ok(const sensor_snapshot_t *sensor_snapshot)
{
    return sensor_snapshot != 0 && !sensor_snapshot->resource_ok;
}

/** @brief 检测位置有效性报警。 */
static bool detect_position_not_ok(const sensor_snapshot_t *sensor_snapshot)
{
    return sensor_snapshot != 0 && !sensor_snapshot->position_ok;
}

static const alarm_rule_t s_alarm_rules[] = {
    {"estop_active", detect_estop_active},
    {"safety_not_ok", detect_safety_not_ok},
    {"resource_not_ok", detect_resource_not_ok},
    {"position_not_ok", detect_position_not_ok},
};

/**
 * @brief 按既定优先级提取首个命中的报警码。
 * @param sensor_snapshot 本次采样快照。
 * @return 命中的报警码；无报警时返回 `0`。
 */
static const char *detect_fault_code(const sensor_snapshot_t *sensor_snapshot)
{
    size_t index;

    for (index = 0u; index < (sizeof(s_alarm_rules) / sizeof(s_alarm_rules[0])); ++index)
    {
        if (s_alarm_rules[index].detector(sensor_snapshot))
        {
            return s_alarm_rules[index].fault_code;
        }
    }

    return 0;
}

/**
 * @brief 按现有告警规则顺序查询故障优先级。
 * @param fault_code 故障码；允许传入 `0`。
 * @return 索引越小优先级越高；未命中时返回规则数量。
 */
static size_t fault_priority(const char *fault_code)
{
    size_t index;

    if (fault_code == 0)
    {
        return sizeof(s_alarm_rules) / sizeof(s_alarm_rules[0]);
    }

    for (index = 0u; index < (sizeof(s_alarm_rules) / sizeof(s_alarm_rules[0])); ++index)
    {
        if (s_alarm_rules[index].fault_code == fault_code)
        {
            return index;
        }
    }

    return sizeof(s_alarm_rules) / sizeof(s_alarm_rules[0]);
}

void alarm_evaluator_init(alarm_evaluator_t *alarm_evaluator)
{
    if (alarm_evaluator == 0)
    {
        return;
    }

    alarm_evaluator->alarm_active = false;
    alarm_evaluator->pending_fault_present = false;
    alarm_evaluator->pending_fault_code = 0;
    alarm_evaluator->pending_fault_occurred_at_ms = 0ul;
}

bool alarm_evaluator_evaluate(alarm_evaluator_t *alarm_evaluator,
                              const sensor_snapshot_t *sensor_snapshot,
                              unsigned long occurred_at_ms,
                              const char **fault_code,
                              unsigned long *fault_occurred_at_ms)
{
    const char *detected_fault_code;

    if (fault_code != 0)
    {
        *fault_code = 0;
    }
    if (fault_occurred_at_ms != 0)
    {
        *fault_occurred_at_ms = 0ul;
    }
    if (alarm_evaluator == 0 || sensor_snapshot == 0)
    {
        return false;
    }
    detected_fault_code = detect_fault_code(sensor_snapshot);
    if (alarm_evaluator->pending_fault_present)
    {
        if (detected_fault_code != 0 &&
            fault_priority(detected_fault_code) < fault_priority(alarm_evaluator->pending_fault_code))
        {
            alarm_evaluator->pending_fault_code = detected_fault_code;
            alarm_evaluator->pending_fault_occurred_at_ms = occurred_at_ms;
        }
        if (fault_code != 0)
        {
            *fault_code = alarm_evaluator->pending_fault_code;
        }
        if (fault_occurred_at_ms != 0)
        {
            *fault_occurred_at_ms = alarm_evaluator->pending_fault_occurred_at_ms;
        }
        return true;
    }
    if (detected_fault_code == 0)
    {
        alarm_evaluator->alarm_active = false;
        return false;
    }
    if (alarm_evaluator->alarm_active)
    {
        return false;
    }

    alarm_evaluator->pending_fault_present = true;
    alarm_evaluator->pending_fault_code = detected_fault_code;
    alarm_evaluator->pending_fault_occurred_at_ms = occurred_at_ms;
    if (fault_code != 0)
    {
        *fault_code = detected_fault_code;
    }
    if (fault_occurred_at_ms != 0)
    {
        *fault_occurred_at_ms = occurred_at_ms;
    }
    return true;
}

/**
 * @brief 在外部告警事件成功投递后提交本轮消费状态。
 * @param alarm_evaluator 判定器对象，不能为空。
 */
void alarm_evaluator_mark_delivered(alarm_evaluator_t *alarm_evaluator)
{
    if (alarm_evaluator == 0)
    {
        return;
    }

    alarm_evaluator->alarm_active = true;
    alarm_evaluator->pending_fault_present = false;
    alarm_evaluator->pending_fault_code = 0;
    alarm_evaluator->pending_fault_occurred_at_ms = 0ul;
}
