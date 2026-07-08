/**
 * @file    port_registry.c
 * @brief   所有端口注册器实现（register / get_ops 函数的统一存放）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    bootstrap/wiring.c 在启动时调用各 xxx_register()，
 *          domain/application 层通过 xxx_get_ops() 获取实现指针。
 *          若某个端口未注册即被使用，get_ops() 返回 NULL，
 *          调用方应在使用前做 assert(ops != NULL) 检查。
 */

#include "framework/ports/outbound/hal/hal_sensor_port.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/ports/outbound/hal/hal_vfd_port.h"
#include "framework/ports/outbound/hal/hal_do_group_port.h"
#include "framework/ports/outbound/hal/hal_voice_port.h"
#include "framework/ports/outbound/cloud/report/report_port.h"
#include "framework/ports/outbound/cloud/connection/connection_port.h"
#include "framework/ports/inbound/command/command_port.h"
#include "framework/ports/outbound/storage/param_store.h"
#include "framework/ports/outbound/storage/deploy_store.h"
#include "framework/ports/inbound/safety/alarm_binding_port.h"

/* -------------------------------------------------------------------------
 * HAL — 传感器
 * ------------------------------------------------------------------------- */
static const hal_sensor_ops_t *s_sensor_ops;

void hal_sensor_register(const hal_sensor_ops_t *ops) { s_sensor_ops = ops; }
const hal_sensor_ops_t *hal_sensor_get_ops(void)      { return s_sensor_ops; }

/* -------------------------------------------------------------------------
 * HAL — IO
 * ------------------------------------------------------------------------- */
static const hal_io_ops_t *s_io_ops;

void hal_io_register(const hal_io_ops_t *ops) { s_io_ops = ops; }
const hal_io_ops_t *hal_io_get_ops(void)      { return s_io_ops; }

/* -------------------------------------------------------------------------
 * HAL — 变频器
 * ------------------------------------------------------------------------- */
static const hal_vfd_ops_t *s_vfd_ops;

void hal_vfd_register(const hal_vfd_ops_t *ops) { s_vfd_ops = ops; }
const hal_vfd_ops_t *hal_vfd_get_ops(void)      { return s_vfd_ops; }

/* -------------------------------------------------------------------------
 * HAL — DO 组×槽位
 * ------------------------------------------------------------------------- */
static const hal_do_group_ops_t *s_do_group_ops;

void hal_do_group_register(const hal_do_group_ops_t *ops) { s_do_group_ops = ops; }
const hal_do_group_ops_t *hal_do_group_get_ops(void)      { return s_do_group_ops; }

/* -------------------------------------------------------------------------
 * 云端 — 上报
 * ------------------------------------------------------------------------- */
static const cloud_report_ops_t *s_report_ops;

void cloud_report_register(const cloud_report_ops_t *ops) { s_report_ops = ops; }
const cloud_report_ops_t *cloud_report_get_ops(void)      { return s_report_ops; }

/* -------------------------------------------------------------------------
 * 云端 — 连接
 * ------------------------------------------------------------------------- */
static const cloud_connection_ops_t *s_connection_ops;

void cloud_connection_register(const cloud_connection_ops_t *ops) { s_connection_ops = ops; }
const cloud_connection_ops_t *cloud_connection_get_ops(void)      { return s_connection_ops; }

/* -------------------------------------------------------------------------
 * 云端 — 命令接入
 * ------------------------------------------------------------------------- */
static const command_port_ops_t *s_command_ops;

void command_port_register(const command_port_ops_t *ops) { s_command_ops = ops; }
const command_port_ops_t *command_port_get_ops(void)      { return s_command_ops; }

/* -------------------------------------------------------------------------
 * 存储 — 运行期参数
 * ------------------------------------------------------------------------- */
static const param_store_ops_t *s_param_ops;

void param_store_register(const param_store_ops_t *ops) { s_param_ops = ops; }
const param_store_ops_t *param_store_get_ops(void)      { return s_param_ops; }

/* -------------------------------------------------------------------------
 * 存储 — 部署期配置
 * ------------------------------------------------------------------------- */
static const deploy_store_ops_t *s_deploy_ops;

void deploy_store_register(const deploy_store_ops_t *ops) { s_deploy_ops = ops; }
const deploy_store_ops_t *deploy_store_get_ops(void)      { return s_deploy_ops; }

/* -------------------------------------------------------------------------
 * HAL — 语音模块
 * ------------------------------------------------------------------------- */
static const hal_voice_ops_t *s_voice_ops;

void hal_voice_register(const hal_voice_ops_t *ops) { s_voice_ops = ops; }
const hal_voice_ops_t *hal_voice_get_ops(void)      { return s_voice_ops; }

/* -------------------------------------------------------------------------
 * 安全 — 报警绑定（项目 adapters → framework/domain/safety，入站端口）
 * ------------------------------------------------------------------------- */
static const alarm_binding_ops_t *s_alarm_binding_ops;

void alarm_binding_register(const alarm_binding_ops_t *ops) { s_alarm_binding_ops = ops; }
const alarm_binding_ops_t *alarm_binding_get_ops(void)      { return s_alarm_binding_ops; }
