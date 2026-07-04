/**
 * @file    alarm_binding_port.h
 * @brief   报警绑定端口（入向：projects/<project>/adapters → framework/domain/safety）
 * @author  HUWANGWEI
 * @date    2026-06-26
 *
 * @note    项目适配器检测到硬件信号变化后，通过本端口把「报警码激活/清除」
 *          推入 framework/domain/safety/alarm/alarm_core，避免 adapter 直接
 *          #include framework/domain/safety/alarm 的实现头文件。
 *          端口实现由 alarm_core 在 alarm_core_init() 中注册。
 */

#ifndef PORTS_SAFETY_ALARM_BINDING_PORT_H
#define PORTS_SAFETY_ALARM_BINDING_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"
#include "framework/domain/safety/model/alarm_code.h"
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 报警绑定操作表
 * ------------------------------------------------------------------------- */
typedef struct
{
    /**
     * @brief  置位报警（已激活则幂等返回 SW_OK）
     * @param  alarm_code  报警码（alarm_code_t 取值）
     * @retval SW_OK / SW_ERR_PARAM（未知报警码）
     */
    sw_err_t (*trigger)(uint32_t alarm_code);

    /**
     * @brief  清除报警（未激活则幂等返回 SW_OK）
     * @param  alarm_code  报警码（alarm_code_t 取值）
     * @retval SW_OK / SW_ERR_PARAM（未知报警码）
     */
    sw_err_t (*clear)(uint32_t alarm_code);

    /**
     * @brief  加载报警目录（整表替换；由机型适配器在 init 时调用）
     * @param  defs   报警定义数组
     * @param  count  条目数
     * @retval SW_OK / SW_ERR_PARAM / SW_ERR_OVERFLOW
     */
    sw_err_t (*load_catalog)(const alarm_def_t *defs, unsigned count);
} alarm_binding_ops_t;

/* -------------------------------------------------------------------------
 * 注册 / 获取
 * ------------------------------------------------------------------------- */
void                       alarm_binding_register(const alarm_binding_ops_t *ops);
const alarm_binding_ops_t *alarm_binding_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_SAFETY_ALARM_BINDING_PORT_H */
