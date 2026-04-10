/**
 * @file    comp_gantry.h
 * @brief   龙门行走组件接口（封装 VFD + 限位逻辑）
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#ifndef COMP_GANTRY_H
#define COMP_GANTRY_H

#include "common/sw_types.h"
#include "common/sw_error.h"

/**
 * @brief  初始化龙门组件（复位位置计数器）
 */
sw_err_t comp_gantry_init(void);

/**
 * @brief  龙门前进（向车头方向）
 * @param  freq_hz  频率（0.01Hz）
 * @retval SW_OK / SW_ERR_STATE（已在前限位时拒绝）
 */
sw_err_t comp_gantry_fwd(uint16_t freq_hz);

/**
 * @brief  龙门后退（向原点方向）
 * @param  freq_hz  频率
 * @retval SW_OK / SW_ERR_STATE（已在后限位时拒绝）
 */
sw_err_t comp_gantry_rev(uint16_t freq_hz);

/**
 * @brief  龙门停止
 */
sw_err_t comp_gantry_stop(void);

/**
 * @brief  归位：以慢速后退直至触发后限位
 * @param  slow_freq  归位速度（0.01Hz）
 * @param  timeout_ms 超时保护（ms），超时后停止并返回 SW_ERR_TIMEOUT
 * @retval SW_OK / SW_ERR_TIMEOUT / SW_ERR_COMM
 * @note   阻塞调用，调用者需在独立线程或状态机中使用
 */
sw_err_t comp_gantry_home(uint16_t slow_freq, uint32_t timeout_ms);

/**
 * @brief  查询是否在前限位
 */
bool comp_gantry_at_fwd_limit(void);

/**
 * @brief  查询是否在后限位（原点）
 */
bool comp_gantry_at_rev_limit(void);

/**
 * @brief  获取当前码盘脉冲计数（上电后单调递增，归位后清零）
 * @retval 脉冲数
 */
int32_t comp_gantry_get_pos(void);

/**
 * @brief  清零脉冲计数（归位完成后调用）
 */
void comp_gantry_reset_pos(void);

/**
 * @brief  由 IO 输入回调调用，每次码盘脉冲触发时更新位置计数
 * @param  direction  当前运动方向（true=前进，false=后退）
 */
void comp_gantry_encoder_tick(bool direction);

#endif /* COMP_GANTRY_H */
