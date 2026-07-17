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
#include "ports/outbound/hal/motor/hal_motor_exec_port.h"

#include <stddef.h>
#include <stdbool.h>

/**
 * @brief  流程类运动故障回调（项目层映射为 alarm trigger）
 */
typedef void (*motion_process_fault_fn_t)(bool is_positive_dir, hal_motor_fault_code_t fault);

/**
 * @brief  运动模式 lifecycle 注入选项
 */
typedef struct {
    actuator_id_t             motion_actuator_id;
    motion_process_fault_fn_t on_process_fault;
} motion_lifecycle_opts_t;

/**
 * @brief  发布运动完成事件（actuator_id 为 0 时忽略）
 * @param[in] opts  lifecycle 选项，可为 NULL
 */
static inline void motion_lifecycle_publish_completed(const motion_lifecycle_opts_t *opts)
{
    if ((opts != NULL) && (opts->motion_actuator_id != 0U)) {
        actuator_publish_motion_completed(opts->motion_actuator_id);
    }
}

/**
 * @brief  上报流程类运动故障（回调为空时忽略）
 * @param[in] opts             lifecycle 选项，可为 NULL
 * @param[in] is_positive_dir  是否正向
 * @param[in] fault            故障码
 */
static inline void motion_lifecycle_report_fault(const motion_lifecycle_opts_t *opts,
                                                 bool                          is_positive_dir,
                                                 hal_motor_fault_code_t        fault)
{
    if ((opts != NULL) && (opts->on_process_fault != NULL)) {
        opts->on_process_fault(is_positive_dir, fault);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_CONTROL_PATTERNS_MOTION_LIFECYCLE_H */
