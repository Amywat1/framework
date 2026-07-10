/**
 * @file    port_registry.c
 * @brief   端口注册器（初始最小集，随模块迁移逐步扩展）
 *
 * @note    完整版见 Project/framework/framework/ports/port_registry.c；
 *          迁入 command/cloud/safety 等端口头文件后，再合并对应 register/get 实现。
 */

#include "ports/outbound/hal/hal_io_port.h"
#include "ports/outbound/storage/deploy_store.h"
#include "ports/outbound/storage/param_store.h"

static const hal_io_ops_t *s_io_ops;

void hal_io_register(const hal_io_ops_t *ops)
{
    s_io_ops = ops;
}

const hal_io_ops_t *hal_io_get_ops(void)
{
    return s_io_ops;
}

static const param_store_ops_t *s_param_ops;

void param_store_register(const param_store_ops_t *ops)
{
    s_param_ops = ops;
}

const param_store_ops_t *param_store_get_ops(void)
{
    return s_param_ops;
}

static const deploy_store_ops_t *s_deploy_ops;

void deploy_store_register(const deploy_store_ops_t *ops)
{
    s_deploy_ops = ops;
}

const deploy_store_ops_t *deploy_store_get_ops(void)
{
    return s_deploy_ops;
}
