/**
 * @file    hw_estop_sim.h
 * @brief   仿真硬件急停输入（Demo / 单元测试用）
 */

#ifndef ADAPTERS_OUTBOUND_SAFETY_SIM_HW_ESTOP_SIM_H
#define ADAPTERS_OUTBOUND_SAFETY_SIM_HW_ESTOP_SIM_H

#include <stdbool.h>

/**
 * @brief  设置仿真急停 DI 状态
 * @param  active  true 表示急停激活（按下）
 */
void hw_estop_sim_set_active(bool active);

/**
 * @brief  读取当前仿真急停 DI 状态
 */
bool hw_estop_sim_get_active(void);

#endif /* ADAPTERS_OUTBOUND_SAFETY_SIM_HW_ESTOP_SIM_H */
