/**
 * @file    hal_sensor_port.h
 * @brief   DI 通道滤波 HAL 端口（不含业务语义）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    对原始 DI 做极性转换与计数防抖，输出稳定逻辑态；
 *          业务映射与报警联动由 machine 层完成；通道绑定由具体
 *          HAL 组合层提供装配接口，port ops 不承载项目点位绑定。
 */

#ifndef PORTS_HAL_SENSOR_PORT_H
#define PORTS_HAL_SENSOR_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/io_handle.h"
#include "common/sw_error.h"

#include <stdbool.h>
#include <stdint.h>

/** 最大滤波通道数（channel 编号 0 .. HAL_SENSOR_CHANNEL_MAX-1） */
#define HAL_SENSOR_CHANNEL_MAX 128U

typedef uint8_t hal_sensor_channel_t;

/** @brief 滤波后逻辑信号状态。 */
typedef enum {
    HAL_SENSOR_STATE_UNKNOWN = 0, /**< 原始输入当前不可信或尚未完成防抖确认。 */
    HAL_SENSOR_STATE_INACTIVE,    /**< 已确认释放。 */
    HAL_SENSOR_STATE_ACTIVE,      /**< 已确认触发。 */
} hal_sensor_state_t;

/**
 * @brief 逻辑信号状态变化回调。
 * @param ch     信号通道。
 * @param state  新状态。
 * @param ctx    注册时传入的上下文。
 */
typedef void (*hal_sensor_state_cb_t)(hal_sensor_channel_t ch, hal_sensor_state_t state, void *ctx);

/**
 * @brief  单通道滤波绑定参数
 */
typedef struct {
    io_di_t pin;           /**< DI 句柄；IO_HANDLE_NULL 表示未安装 */
    bool    active_low;    /**< true=低电平有效 */
    uint8_t trig_count;    /**< 触发确认连续采样次数 */
    uint8_t release_count; /**< 释放确认连续采样次数 */
} hal_sensor_bind_cfg_t;

typedef struct {
    /** @brief  初始化内部运行时状态 */
    sw_err_t (*init)(void);

    /**
     * @brief  同步执行指定轮次采样，用于启动阶段预填充滤波状态。
     * @param  sample_count 采样轮次；0 表示不执行采样。
     * @retval SW_OK        预热完成。
     * @retval SW_ERR_NOT_INIT 依赖的 IO 后端未就绪。
     * @note   本接口只用于初始化预热，不用于项目层创建运行期轮询线程；
     *         运行期滤波推进由 framework sensor_filter poll 任务负责。
     */
    sw_err_t (*warmup)(uint8_t sample_count);

    /** @brief 查询通道滤波后的稳定逻辑态；UNKNOWN 返回 false。 */
    bool (*is_active)(hal_sensor_channel_t ch);

    /** @brief 查询通道三态结果；参数无效或未绑定时返回 UNKNOWN。 */
    hal_sensor_state_t (*get_state)(hal_sensor_channel_t ch);

    /**
     * @brief 订阅逻辑信号状态变化。
     * @retval SW_OK / SW_ERR_PARAM / SW_ERR_OVERFLOW
     * @note   回调在传感器采样任务上下文同步执行，必须在有界时间内返回；
     *         允许短临界区、安全切断和非阻塞事件投递，禁止 sleep、
     *         等待外部 IO、动态内存分配及其他无界阻塞。
     */
    sw_err_t (*subscribe)(hal_sensor_state_cb_t cb, void *ctx);
} hal_sensor_ops_t;

void                    hal_sensor_register(const hal_sensor_ops_t *ops);
const hal_sensor_ops_t *hal_sensor_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_SENSOR_PORT_H */
