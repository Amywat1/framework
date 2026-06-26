/**
 * @file    m8_alarm_adapt.h
 * @brief   M8 机型报警绑定适配（硬件信号 → 报警码）
 * @author  HUWANGWEI
 * @date    2026-06-26
 *
 * @note    维护 M8 DI 信号到报警码的映射表，周期性读取防抖后的信号电平，
 *          在电平翻转时通过 ports/safety/alarm_binding_port 把报警激活/清除
 *          推入 domain/safety/alarm_core。本适配器只依赖端口，不直接依赖
 *          domain/safety 实现。
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
 * @brief  执行一次报警信号轮询（边沿检测 + 激活/清除）
 * @note   由 io_poll 线程在 m8_signal_filter_tick() 之后周期调用；
 *         场景测试可在 filter tick 后直接调用以替代线程
 */
void m8_alarm_adapt_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_ALARM_ADAPT_H */
