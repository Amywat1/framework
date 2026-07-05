/**
 * @file    hal_sensor_port.h
 * @brief   DI 通道滤波 HAL 端口（不含业务语义）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    对原始 DI 做极性转换与计数防抖，输出稳定逻辑态；
 *          业务映射与报警联动由 machine 层完成。
 *          通道绑定通过 hal_sensor_ops_t.bind() 完成，由已注册的
 *          hal_sensor 适配器（如 hal_sensor_filter）提供具体实现。
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

/** 最大滤波通道数（channel 编号 0 .. HAL_SENSOR_CHANNEL_MAX-1） */
#define HAL_SENSOR_CHANNEL_MAX  128U

typedef uint8_t hal_sensor_channel_t;

/**
 * @brief  单通道滤波绑定参数
 */
typedef struct
{
    io_di_t  pin;             /**< DI 句柄；IO_HANDLE_NULL 表示未安装 */
    bool     active_low;      /**< true=低电平有效 */
    uint8_t  trig_count;      /**< 触发确认连续采样次数 */
    uint8_t  release_count;   /**< 释放确认连续采样次数 */
} hal_sensor_bind_cfg_t;

typedef struct
{
    /** @brief  初始化内部运行时状态 */
    sw_err_t (*init)(void);

    /**
     * @brief  绑定传感器通道滤波参数。
     * @param  ch   通道编号。
     * @param  cfg  绑定配置，trig_count/release_count 必须大于 0。
     * @retval SW_OK        绑定成功。
     * @retval SW_ERR_PARAM 参数非法。
     * @note   供项目 wiring/bindings 在启动阶段调用。
     */
    sw_err_t (*bind)(hal_sensor_channel_t ch, const hal_sensor_bind_cfg_t *cfg);

    /**
     * @brief  同步执行指定轮次采样，用于启动阶段预填充滤波状态。
     * @param  sample_count 采样轮次；0 表示不执行采样。
     * @retval SW_OK        预热完成。
     * @retval SW_ERR_NOT_INIT 依赖的 IO 后端未就绪。
     * @note   本接口只用于初始化预热，不用于项目层创建运行期轮询线程；
     *         运行期滤波推进由 framework sensor_filter poll 任务负责。
     */
    sw_err_t (*warmup)(uint8_t sample_count);

    /** @brief  查询通道滤波后的稳定逻辑态 */
    bool (*is_active)(hal_sensor_channel_t ch);
} hal_sensor_ops_t;

void                        hal_sensor_register(const hal_sensor_ops_t *ops);
const hal_sensor_ops_t     *hal_sensor_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_SENSOR_PORT_H */
