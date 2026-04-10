/**
 * @file    hal_water_port.h
 * @brief   水路控制 HAL 端口接口
 * @author  胡望伟
 * @date    2026-04-10
 */

#ifndef PORTS_HAL_WATER_PORT_H
#define PORTS_HAL_WATER_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 水路控制操作表
 * ------------------------------------------------------------------------- */
typedef struct
{
    sw_err_t (*pump_set)(bool on);        /* 水泵启停（其余水路自动随泵启动）*/
    sw_err_t (*curtain_set)(bool on);     /* 清水水帘阀 */
    sw_err_t (*foam_set)(bool on);        /* 泡沫阀 */
    sw_err_t (*brush_water_set)(bool on); /* 刷子冲水阀 */
    sw_err_t (*highpres_set)(bool on);    /* 高压冲洗阀 */

    /** @brief 关闭所有水路（水泵 + 全部水阀）*/
    sw_err_t (*all_off)(void);
} hal_water_ops_t;

/* -------------------------------------------------------------------------
 * 注册 / 获取
 * ------------------------------------------------------------------------- */
void                  hal_water_register(const hal_water_ops_t *ops);
const hal_water_ops_t *hal_water_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_WATER_PORT_H */
