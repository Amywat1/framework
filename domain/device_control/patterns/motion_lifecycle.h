/**
 * @file    motion_lifecycle.h
 * @brief   运动模式 lifecycle 钩子（不含报警语义）
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef DOMAIN_DEVICE_CONTROL_PATTERNS_MOTION_LIFECYCLE_H
#define DOMAIN_DEVICE_CONTROL_PATTERNS_MOTION_LIFECYCLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/device_control/model/actuator_events.h"
#include "domain/ports/outbound/motor/hal_motor_exec_port.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief  本次运动结局类别（与空闲边沿正交）
 */
typedef enum {
    MOTOR_AXIS_OUTCOME_ARRIVED = 0, /**< 正常到位 */
    MOTOR_AXIS_OUTCOME_TIMEOUT,     /**< 超时兜底 */
    MOTOR_AXIS_OUTCOME_STOPPED,     /**< 显式停止 */
    MOTOR_AXIS_OUTCOME_FAULT,       /**< 故障 */
    MOTOR_AXIS_OUTCOME_ESTOP        /**< 急停 */
} motor_axis_outcome_t;

/**
 * @brief  运动结局详情（供项目分支与报警映射）
 */
typedef struct {
    bool                      valid;      /**< 是否曾记录过结局 */
    motor_axis_outcome_t      outcome;    /**< 结局类别 */
    hal_motor_end_condition_t trigger;    /**< 结束条件（到位原因） */
    bool                      has_limit;  /**< limit 是否有效 */
    hal_motor_limit_kind_t    limit;      /**< 硬限位种类 */
    int64_t                   final_pos;  /**< 结束位置（脉冲） */
    uint64_t                  elapsed_ms; /**< 运动耗时（ms） */
    hal_motor_fault_code_t    fault;      /**< 故障码；无故障为 NONE */
} motor_axis_end_result_t;

/**
 * @brief  运动结局回调（项目层做流程分支或报警映射）
 * @param  id      注入的 actuator_id；可为 0
 * @param  result  结局详情，非 NULL
 * @note   同一故障闩锁下 axis 会对同 outcome+fault 去重；项目层仍应按故障码幂等处理。
 */
typedef void (*motion_process_end_fn_t)(actuator_id_t id, const motor_axis_end_result_t *result);

/**
 * @brief  运动模式 lifecycle 注入选项
 * @note   motion_completed 只表示轴空闲（重评估）；结局原因走 on_motion_end / last_result。
 */
typedef struct {
    actuator_id_t           motion_actuator_id;
    motion_process_end_fn_t on_motion_end;
} motion_lifecycle_opts_t;

/**
 * @brief  发布运动空闲事件（actuator_id 为 0 时忽略）
 * @param[in] opts  lifecycle 选项，可为 NULL
 * @note   由 motor_axis_poll() 在轴进入 IDLE 时调用；不携带到位原因
 */
static inline void motion_lifecycle_publish_completed(const motion_lifecycle_opts_t *opts)
{
    if ((opts != NULL) && (opts->motion_actuator_id != 0U)) {
        actuator_publish_motion_completed(opts->motion_actuator_id);
    }
}

/**
 * @brief  上报运动结局（回调为空或 result 无效时忽略）
 * @param[in] opts    lifecycle 选项，可为 NULL
 * @param[in] result  结局详情
 */
static inline void motion_lifecycle_report_end(const motion_lifecycle_opts_t *opts,
                                               const motor_axis_end_result_t *result)
{
    if ((opts != NULL) && (opts->on_motion_end != NULL) && (result != NULL) && result->valid) {
        opts->on_motion_end(opts->motion_actuator_id, result);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_CONTROL_PATTERNS_MOTION_LIFECYCLE_H */
