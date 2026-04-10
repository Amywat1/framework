/**
 * @file    comp_top_lift.h
 * @brief   顶刷升降组件接口（雷赛步进电机，限位开关保护）
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#ifndef COMP_TOP_LIFT_H
#define COMP_TOP_LIFT_H

#include "common/sw_types.h"
#include "common/sw_error.h"

/**
 * @brief  初始化顶刷升降组件
 */
sw_err_t comp_top_lift_init(void);

/**
 * @brief  顶刷上升至上限位（限位开关保护）
 * @param  timeout_ms  超时（ms）
 * @retval SW_OK / SW_ERR_TIMEOUT / SW_ERR_STATE
 */
sw_err_t comp_top_lift_up(uint32_t timeout_ms);

/**
 * @brief  顶刷下降指定脉冲数（开环，依赖参数 topLiftDownPulses）
 * @param  pulses  脉冲数（0 = 读取参数表中配置值）
 * @retval SW_OK / SW_ERR_STATE（已在下限位时拒绝）
 */
sw_err_t comp_top_lift_down(uint32_t pulses);

/**
 * @brief  查询是否在上限位
 */
bool comp_top_lift_at_top(void);

/**
 * @brief  查询是否在下限位
 */
bool comp_top_lift_at_bottom(void);

#endif /* COMP_TOP_LIFT_H */
