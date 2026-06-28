/**
 * @file    m8_alarm_adapt.h
 * @brief   M8 机型报警绑定适配（硬件信号 → 报警码）
 * @author  HUWANGWEI
 * @date    2026-06-26
 *
 * @note    自包含的 M8 报警检测适配器：通过 X-macro 单表同时定义 DI 引脚、
 *          极性、独立防抖参数（trig/rel 计数）与报警定义（码/等级/清除/描述）。
 *          poll 直接读原始 DI（hal_io_port），内部独立防抖，不依赖 m8_sensor。
 *          init 时通过 alarm_binding_port.load_catalog 向 alarm_core 注册目录。
 */

#ifndef ADAPTERS_MACHINE_M8_ALARM_ADAPT_H
#define ADAPTERS_MACHINE_M8_ALARM_ADAPT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  初始化报警适配（校验绑定端口已注册，复位边沿检测基线）
 * @retval SW_OK / SW_ERR_NOT_INIT（alarm_binding_port 未注册）
 * @note   须在 alarm_core_init() 之后调用
 */
sw_err_t m8_alarm_adapt_init(void);

/**
 * @brief  执行一次报警信号轮询（独立防抖 + 边沿检测 + 激活/清除）
 * @note   场景测试可直接调用以替代线程
 */
void m8_alarm_adapt_poll(void);

/**
 * @brief  启动报警轮询线程（模块自管，不经 scheduler）
 * @note   须在 m8_alarm_adapt_init() 之后调用
 */
sw_err_t m8_alarm_adapt_poll_start(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_ALARM_ADAPT_H */
