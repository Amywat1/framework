/**
 * @file    gantry.h
 * @brief   龙门行走设备接口
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    通过 hal_motion_port + hal_sensor_port 接口控制，不依赖驱动层。
 *          码盘脉冲计数由 IO 回调触发（encoder_tick），使用原子操作。
 *          gantry_home_start() 非阻塞：启动后退，事件驱动停止并发 EVT_COMP_HOME_DONE。
 */

#ifndef DOMAIN_DEVICE_GANTRY_H
#define DOMAIN_DEVICE_GANTRY_H

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
 * @brief  初始化龙门组件（清零位置计数，订阅限位事件）
 */
sw_err_t gantry_init(void);

/**
 * @brief  龙门前进
 * @param  freq_hz  VFD 频率（0.01Hz）
 */
sw_err_t gantry_fwd(uint16_t freq_hz);

/**
 * @brief  龙门后退
 * @param  freq_hz  VFD 频率（0.01Hz）
 */
sw_err_t gantry_rev(uint16_t freq_hz);

/**
 * @brief  龙门停止
 */
sw_err_t gantry_stop(void);

/**
 * @brief  开始归位（非阻塞）
 *         启动后退 VFD，监听后限位事件，到达后自动停止、清零位置并发布 EVT_COMP_HOME_DONE。
 * @param  freq_hz  归位速度（0.01Hz，建议慢速）
 * @retval SW_OK       归位启动成功（或已在后限位，立即发 EVT_COMP_HOME_DONE）
 * @retval SW_ERR_BUSY 正在归位中
 */
sw_err_t gantry_home_start(uint16_t freq_hz);

/**
 * @brief  查询龙门是否在前限位
 */
bool gantry_at_fwd_limit(void);

/**
 * @brief  查询龙门是否在后限位
 */
bool gantry_at_rev_limit(void);

/**
 * @brief  获取当前位置（码盘脉冲数，归位后为 0）
 * @note   直接委托给 hal_sensor_get_ops()->get_gantry_pos()（Option A）；
 *         位置由 HAL 层（m8_hal_ctx.c）通过编码器回调原子累加维护。
 */
int32_t gantry_get_pos(void);

/**
 * @brief  重置位置计数（归位完成后由 home 逻辑调用）
 */
void gantry_reset_pos(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_GANTRY_H */
