/**
 * @file    deploy_store.c
 * @brief   部署期配置存储端口注册表
 */

#include "ports/outbound/storage/deploy_store.h"

static const deploy_store_ops_t *s_ops;

void deploy_store_register(const deploy_store_ops_t *ops)
{
    s_ops = ops;
}

const deploy_store_ops_t *deploy_store_get_ops(void)
{
    return s_ops;
}
