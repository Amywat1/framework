/**
 * @file    port_registry_infra.c
 * @brief   基础设施端口注册表（设备命令 / 报警绑定 / 机型运行时操作）
 *
 * @note    注册语义见 runtime/ports/port_registry.h。
 */

#include "application/ports/inbound/command/command_port.h"
#include "application/ports/inbound/safety/alarm_binding_port.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "runtime/ports/port_registry.h"

#include <stddef.h>

/* ---- 设备命令 ---- */
static const device_command_port_ops_t *s_cmd_ops;

sw_err_t device_command_port_register(const device_command_port_ops_t *ops)
{
    /* async/sync 均为必填；缺失则命令链路不可用 */
    if ((ops != NULL) && ((ops->submit_async == NULL) || (ops->submit_sync == NULL))) {
        return SW_ERR_PARAM;
    }
    s_cmd_ops = ops;
    return SW_OK;
}

const device_command_port_ops_t *device_command_port_get_ops(void)
{
    return s_cmd_ops;
}

/* ---- 报警绑定 ---- */
static const alarm_binding_ops_t *s_alarm_binding_ops;

sw_err_t alarm_binding_register(const alarm_binding_ops_t *ops)
{
    /* 三者均被适配器无条件调用，缺任一项都会在运行期空指针解引用 */
    if ((ops != NULL) && ((ops->trigger == NULL) || (ops->clear == NULL) || (ops->load_catalog == NULL))) {
        return SW_ERR_PARAM;
    }
    s_alarm_binding_ops = ops;
    return SW_OK;
}

const alarm_binding_ops_t *alarm_binding_get_ops(void)
{
    return s_alarm_binding_ops;
}

/* ---- 机型运行时操作 ---- */
static const device_ops_t *s_device_ops;

sw_err_t device_ops_register(const device_ops_t *ops)
{
    /* 各字段由 side_effect_router 逐个判空，机型可只实现子集，故无必填项 */
    s_device_ops = ops;
    return SW_OK;
}

const device_ops_t *device_ops_get(void)
{
    return s_device_ops;
}

void port_registry_infra_reset(void)
{
    s_cmd_ops           = NULL;
    s_alarm_binding_ops = NULL;
    s_device_ops       = NULL;
}
