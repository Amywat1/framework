/**
 * @file    port_registry.c
 * @brief   端口注册器（随模块迁移逐步扩展）
 *
 * @note    完整版见 Project/framework/framework/ports/port_registry.c；
 *          迁入 command/cloud/safety 等端口头文件后，再合并对应 register/get 实现。
 */

#include "ports/outbound/hal/hal_sensor_port.h"
#include "ports/outbound/hal/hal_io_port.h"
#include "ports/outbound/hal/hal_vfd_port.h"
#include "ports/outbound/hal/hal_voice_port.h"
#include "ports/outbound/storage/deploy_store.h"
#include "ports/outbound/storage/param_store.h"

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
 * HAL — 语音模块
 * ------------------------------------------------------------------------- */
static const hal_voice_ops_t *s_voice_ops;

void hal_voice_register(const hal_voice_ops_t *ops) { s_voice_ops = ops; }
const hal_voice_ops_t *hal_voice_get_ops(void)      { return s_voice_ops; }

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
