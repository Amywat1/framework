/**
 * @file    alarm_binding_port.h
 * @brief   报警绑定端口（入向：projects/<project>/adapters → domain/safety）
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    项目适配器检测到硬件信号变化后，通过本端口把「报警码激活/清除」
 *          推入 alarm_registry，避免 adapter 直接 #include 实现头文件。
 *          端口实现由 alarm_registry_init() 注册。
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
    /** 注入「急停免疫」判断函数，用于区分允许跳过急停的报警码 */
    void     (*set_estop_skip_fn)(bool (*fn)(uint32_t code));
    /** 注册 ON_MOTION 重评估绑定表并订阅 lifecycle 事件 */
    sw_err_t (*init_reeval_bridge)(const alarm_reeval_binding_t *bindings, size_t count);
} alarm_binding_ops_t;

void                       alarm_binding_register(const alarm_binding_ops_t *ops);
const alarm_binding_ops_t *alarm_binding_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_SAFETY_ALARM_BINDING_PORT_H */
