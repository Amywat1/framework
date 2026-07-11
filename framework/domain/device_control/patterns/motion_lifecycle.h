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

#include "framework/domain/device_control/model/actuator_events.h"
#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include <stdbool.h>

/**
 * @brief  流程类运动故障回调（项目层映射为 alarm trigger）
 */
typedef void (*motion_process_fault_fn_t)(bool is_positive_dir, hal_motor_fault_code_t fault);

/**
 * @brief  运动模式 lifecycle 注入选项
 */
typedef struct
{
    actuator_id_t             motion_actuator_id;
    motion_process_fault_fn_t on_process_fault;
} motion_lifecycle_opts_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_CONTROL_PATTERNS_MOTION_LIFECYCLE_H */
