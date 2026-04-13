/**
 * @file    hal_indicator_port.h
 * @brief   入口指示灯与挡杆 HAL 端口接口
 * @author  胡望伟
 * @date    2026-04-10
 */

#ifndef PORTS_HAL_INDICATOR_PORT_H
#define PORTS_HAL_INDICATOR_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/* -------------------------------------------------------------------------
 * 入口指示灯状态
 * ------------------------------------------------------------------------- */
typedef enum
{
    HAL_LIGHT_OFF = 0,
    HAL_LIGHT_GREEN,          /* 绿灯常亮 */
    HAL_LIGHT_RED,            /* 红灯常亮 */
    HAL_LIGHT_YELLOW,         /* 黄灯常亮 */
    HAL_LIGHT_GREEN_BLINK,    /* 绿灯闪烁（等待车辆进入）*/
    HAL_LIGHT_RED_BLINK,      /* 红灯闪烁（设备故障警告）*/
    HAL_LIGHT_YELLOW_BLINK,   /* 黄灯闪烁（洗车中/归位中）*/
} hal_light_state_t;

/* -------------------------------------------------------------------------
 * 指示灯与挡杆操作表
 * ------------------------------------------------------------------------- */
typedef struct
{
    /** @brief 设置入口指示灯状态（同时控制红/绿/黄三路输出）*/
    sw_err_t (*entry_light_set)(hal_light_state_t state);

    /** @brief 指示灯闪烁 tick（由报警轮询周期调用；可为 NULL 表示不支持闪烁）*/
    void (*entry_light_tick)(void);

    /** @brief 挡杆缩回（放行车辆）*/
    sw_err_t (*rod_open)(void);

    /** @brief 挡杆伸出（拦截车辆）*/
    sw_err_t (*rod_close)(void);
} hal_indicator_ops_t;

/* -------------------------------------------------------------------------
 * 注册 / 获取
 * ------------------------------------------------------------------------- */
void                      hal_indicator_register(const hal_indicator_ops_t *ops);
const hal_indicator_ops_t *hal_indicator_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_INDICATOR_PORT_H */
