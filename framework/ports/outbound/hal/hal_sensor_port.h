/**
 * @file    hal_sensor_port.h
 * @brief   DI 通道滤波 HAL 端口（不含业务语义）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    对原�?DI 做极性转换与计数防抖，输出稳定逻辑态；
 *          业务映射与报警联动由 machine 层完成�?
 *          通道绑定�?framework/adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.h 中的
 *          hal_sensor_bind() 完成�?
 */

#ifndef PORTS_HAL_SENSOR_PORT_H
#define PORTS_HAL_SENSOR_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/io_handle.h"
#include "framework/common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

/** 最大滤波通道数（channel 编号 0 .. HAL_SENSOR_CHANNEL_MAX-1�?*/
#define HAL_SENSOR_CHANNEL_MAX  128U

typedef uint8_t hal_sensor_channel_t;

/**
 * @brief  单通道滤波绑定参数
 */
typedef struct
{
    io_di_t  pin;             /**< DI 句柄；IO_HANDLE_NULL 表示未安�?*/
    bool     active_low;      /**< true=低电平有�?*/
    uint8_t  trig_count;      /**< 触发确认连续采样次数 */
    uint8_t  release_count;   /**< 释放确认连续采样次数 */
} hal_sensor_bind_cfg_t;

typedef struct
{
    /** @brief  初始化内部运行时状�?*/
    sw_err_t (*init)(void);

    /** @brief  执行一轮全通道滤波（由调度层周期调用） */
    void (*tick)(void);

    /** @brief  查询通道滤波后的稳定逻辑�?*/
    bool (*is_active)(hal_sensor_channel_t ch);
} hal_sensor_ops_t;

void                        hal_sensor_register(const hal_sensor_ops_t *ops);
const hal_sensor_ops_t     *hal_sensor_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_SENSOR_PORT_H */
