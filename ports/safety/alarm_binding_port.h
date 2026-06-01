/**
 * @file    alarm_binding_port.h
 * @brief   报警绑定端口——硬件适配器向报警引擎注入信号和回调的契约接口
 *
 * @note    调用方：adapters/machine/m8/m8_alarm_adapt.c（及其他机型适配器）
 *          实现方：domain/safety/alarm_core.c
 *          方向：adapters → domain（适配器主动推送 IO 状态和注册回调）
 */

#ifndef PORTS_SAFETY_ALARM_BINDING_PORT_H
#define PORTS_SAFETY_ALARM_BINDING_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief  IO 信号轮询回调类型——每个报警 tick 由引擎调用一次
 */
typedef void (*alarm_poll_fn_t)(void);

/**
 * @brief  急停复位动作回调类型——手动复位时由引擎调用
 */
typedef void (*alarm_emc_reset_fn_t)(void);

/**
 * @brief  注册 IO 信号轮询回调（由 m8_alarm_adapt_init 在启动时调用）
 */
void alarm_core_register_poll_fn(alarm_poll_fn_t fn);

/**
 * @brief  注册急停复位动作回调（由 m8_alarm_adapt_init 在启动时调用）
 */
void alarm_core_register_emc_reset_fn(alarm_emc_reset_fn_t fn);

/**
 * @brief  设置 IO 轮询类报警的原始触发状态（由 poll_fn 内部调用，经防抖处理）
 * @param  code         报警码
 * @param  triggered    当前是否触发
 * @param  just_notice  true = 强制降级为 NOTICE（硬件未安装场景）
 */
void alarm_core_set_raw_trigger(uint16_t code, bool triggered, bool just_notice);

/**
 * @brief  直接置位/清除报警状态（驱动事件直报，不经防抖）
 * @param  code         报警码
 * @param  active       true = 激活，false = 清除
 * @param  just_notice  true = 强制降级为 NOTICE
 */
void alarm_core_set_state(uint16_t code, bool active, bool just_notice);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_SAFETY_ALARM_BINDING_PORT_H */
