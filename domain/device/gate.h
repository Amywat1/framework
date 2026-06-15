/**
 * @file    gate.h
 * @brief   入口挡杆与指示灯设备接口
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    当前未接入硬件控制；接口保留供状态机调用，实际操作均为空实现。
 */

#ifndef DOMAIN_DEVICE_GATE_H
#define DOMAIN_DEVICE_GATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

typedef enum
{
    GATE_LIGHT_OFF = 0,
    GATE_LIGHT_GREEN,
    GATE_LIGHT_RED,
    GATE_LIGHT_YELLOW,
    GATE_LIGHT_GREEN_BLINK,
    GATE_LIGHT_RED_BLINK,
    GATE_LIGHT_YELLOW_BLINK,
} gate_light_state_t;

/**
 * @brief  初始化入口组件
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
sw_err_t gate_set_light(gate_light_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_GATE_H */
