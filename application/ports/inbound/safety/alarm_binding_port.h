/**
 * @file    alarm_binding_port.h
 * @brief   报警绑定端口（入向：adapters → domain/safety）
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    项目适配器检测到故障条件变化后，通过本端口上报「条件成立/条件消失」。
 *          clear 只表示条件消失，不保证手动锁存告警立即从活动列表删除。
 *          端口实现由 application/bridges/alarm_bridge_bind() 注册。
 */

#ifndef APPLICATION_PORTS_INBOUND_SAFETY_ALARM_BINDING_PORT_H
#define APPLICATION_PORTS_INBOUND_SAFETY_ALARM_BINDING_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/safety/model/alarm_types.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    /** @brief 上报指定报警的故障条件成立。 */
    sw_err_t (*trigger)(uint32_t alarm_code);
    /** @brief 上报指定报警的故障条件消失。 */
    sw_err_t (*clear)(uint32_t alarm_code);
    /** @brief 装载项目报警目录。 */
    sw_err_t (*load_catalog)(const alarm_def_t *defs, unsigned count);
} alarm_binding_ops_t;

/**
 * @brief 注册报警绑定端口实现。
 * @param ops 端口操作表，生命周期必须覆盖后续调用。
 */
sw_err_t alarm_binding_register(const alarm_binding_ops_t *ops);

/**
 * @brief 获取已注册的报警绑定端口实现。
 * @return 已注册的操作表；尚未注册时返回 NULL。
 */
const alarm_binding_ops_t *alarm_binding_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_PORTS_INBOUND_SAFETY_ALARM_BINDING_PORT_H */
