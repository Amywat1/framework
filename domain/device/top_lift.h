/**
 * @file    top_lift.h
 * @brief   顶刷升降设备接口（步进电机驱动）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    lift_up_start / lift_down_start 为非阻塞接口。
 *          Phase 3 HAL 实现为同步（完成后发 EVT_COMP_LIFT_DONE 再返回）；
 *          Phase 5 HAL 改为异步（stepper_thread 执行，立即返回）。
 *          上层无需关心实现差异，统一等待 EVT_COMP_LIFT_DONE 事件。
 */

#ifndef DOMAIN_DEVICE_TOP_LIFT_H
#define DOMAIN_DEVICE_TOP_LIFT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化顶刷升降组件
 */
sw_err_t top_lift_init(void);

/**
 * @brief  顶刷上升（非阻塞，完成后发 EVT_COMP_LIFT_DONE）
 * @param  pulses  最大脉冲数（0 = 使用 HAL 配置默认值）
 */
sw_err_t top_lift_up_start(uint32_t pulses);

/**
 * @brief  顶刷下降（非阻塞，完成后发 EVT_COMP_LIFT_DONE）
 * @param  pulses  脉冲数（0 = 使用 HAL 配置默认值）
 */
sw_err_t top_lift_down_start(uint32_t pulses);

/**
 * @brief  查询顶刷是否在上限位
 */
bool top_lift_at_top(void);

/**
 * @brief  查询顶刷是否在下限位
 */
bool top_lift_at_bottom(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_TOP_LIFT_H */
