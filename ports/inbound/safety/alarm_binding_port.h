/**
 * @file    alarm_binding_port.h
 * @brief   报警绑定端口（入向：adapters → domain/safety）
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    项目适配器检测到硬件信号变化后，通过本端口把「报警码激活/清除」
 *          推入 alarm_registry。端口实现由 alarm_registry_init() 注册。
 */

#ifndef PORTS_SAFETY_ALARM_BINDING_PORT_H
#define PORTS_SAFETY_ALARM_BINDING_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/safety/model/alarm_types.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    sw_err_t (*trigger)(uint32_t alarm_code);
    sw_err_t (*clear)(uint32_t alarm_code);
    sw_err_t (*load_catalog)(const alarm_def_t *defs, unsigned count);
} alarm_binding_ops_t;

void                       alarm_binding_register(const alarm_binding_ops_t *ops);
const alarm_binding_ops_t *alarm_binding_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_SAFETY_ALARM_BINDING_PORT_H */
