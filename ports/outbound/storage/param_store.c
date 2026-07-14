/**
 * @file    param_store.c
 * @brief   运行期参数存储端口注册表
 */

#include "ports/outbound/storage/param_store.h"

static const param_store_ops_t *s_ops;

void param_store_register(const param_store_ops_t *ops)
{
    s_ops = ops;
}

const param_store_ops_t *param_store_get_ops(void)
{
    return s_ops;
}
