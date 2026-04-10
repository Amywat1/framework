/**
 * @file    bsp_init.h
 * @brief   系统初始化序列（顺序严格固定）
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#ifndef BSP_INIT_H
#define BSP_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  执行完整初始化序列
 * @retval SW_OK / SW_ERR_HW
 * @note   顺序：cli → io_init → sleep(2) → hal_init → svc_alarm_init
 *               → bsp_alarm_init → svc_param_init → module_init
 */
sw_err_t bsp_system_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_INIT_H */
