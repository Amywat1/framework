/**
 * @file    gate.h
 * @brief   入口挡杆与指示灯设备接口
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    通过 hal_indicator_port 接口控制。
 *          上电默认：挡杆伸出（拦截），指示灯红色。
 */

#ifndef DOMAIN_DEVICE_GATE_H
#define DOMAIN_DEVICE_GATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ports/hal/hal_indicator_port.h"
#include "common/sw_error.h"

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化入口组件（挡杆关闭，灯红色）
 */
sw_err_t gate_init(void);

/**
 * @brief  放行车辆（挡杆缩回，绿灯）
 */
sw_err_t gate_allow(void);

/**
 * @brief  拦截车辆（挡杆伸出，红灯）
 */
sw_err_t gate_block(void);

/**
 * @brief  设置指示灯状态（不影响挡杆）
 * @param  state  灯光状态
 */
sw_err_t gate_set_light(hal_light_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_GATE_H */
