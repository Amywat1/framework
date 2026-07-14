/**
 * @file    port_registry_infra.c
 * @brief   基础设施端口注册表（设备命令 / 报警绑定 / 机型运行时操作）
 */

#include "ports/inbound/command/command_port.h"
#include "ports/inbound/safety/alarm_binding_port.h"
#include "ports/outbound/machine/machine_ops_port.h"

/* ---- 设备命令 ---- */
static const device_command_port_ops_t *s_cmd_ops;
void device_command_port_register(const device_command_port_ops_t *ops) { s_cmd_ops = ops; }
const device_command_port_ops_t *device_command_port_get_ops(void) { return s_cmd_ops; }

/* ---- 报警绑定 ---- */
static const alarm_binding_ops_t *s_alarm_binding_ops;
void alarm_binding_register(const alarm_binding_ops_t *ops) { s_alarm_binding_ops = ops; }
const alarm_binding_ops_t *alarm_binding_get_ops(void) { return s_alarm_binding_ops; }

/* ---- 机型运行时操作 ---- */
static const machine_ops_t *s_machine_ops;
void machine_ops_register(const machine_ops_t *ops) { s_machine_ops = ops; }
const machine_ops_t *machine_ops_get(void) { return s_machine_ops; }
