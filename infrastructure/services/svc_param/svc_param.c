/**
 * @file    svc_param.c
 * @brief   运行时参数管理实现（通过 param_store_ops 端口访问存储）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    svc_param 是业务语义层，不直接操作存储介质。
 *          底层读写委托给 param_store_ops（由 wiring.c 注册 json_param_store 实现）。
 *          整型参数以字符串形式存入 param_store，读取时做 atoi 转换。
 *          若 param_store 未注册，所有读取返回默认值，写入/保存静默失败。
 */

#include "infrastructure/services/svc_param/svc_param.h"
#include "ports/storage/param_store.h"
#include "common/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 内部辅助：获取 ops 并检查非空
 * ------------------------------------------------------------------------- */
static const param_store_ops_t *get_ops(void)
{
    const param_store_ops_t *ops = param_store_get_ops();
    if (ops == NULL)
    {
        LOG_WARN("svc_param: param_store not registered");
    }
    return ops;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t svc_param_init(void)
{
    const param_store_ops_t *ops = get_ops();
    if (ops == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return ops->load();
}

int svc_param_get_int(const char *key, int default_val)
{
    char                     buf[32];
    const param_store_ops_t *ops = param_store_get_ops();

    if ((ops == NULL) || (key == NULL))
    {
        return default_val;
    }
    if (ops->get(key, buf, sizeof(buf)) != SW_OK)
    {
        return default_val;
    }
    return atoi(buf);
}

sw_err_t svc_param_get_str(const char *key, char *buf, int buf_size,
                            const char *default_val)
{
    const param_store_ops_t *ops = param_store_get_ops();

    if ((buf == NULL) || (buf_size <= 0))
    {
        return SW_ERR_PARAM;
    }

    if ((ops == NULL) || (key == NULL) ||
        (ops->get(key, buf, (size_t)buf_size) != SW_OK))
    {
        /* 未注册或键不存在：写入默认值 */
        strncpy(buf, (default_val != NULL) ? default_val : "",
                (size_t)(buf_size - 1));
        buf[buf_size - 1] = '\0';
    }
    return SW_OK;
}

sw_err_t svc_param_set_int(const char *key, int val)
{
    char                     buf[32];
    const param_store_ops_t *ops = get_ops();

    if (ops == NULL) { return SW_ERR_NOT_INIT; }
    if (key == NULL) { return SW_ERR_PARAM; }

    snprintf(buf, sizeof(buf), "%d", val);
    return ops->set(key, buf);
}

sw_err_t svc_param_set_str(const char *key, const char *val)
{
    const param_store_ops_t *ops = get_ops();

    if (ops == NULL)           { return SW_ERR_NOT_INIT; }
    if ((key == NULL) || (val == NULL)) { return SW_ERR_PARAM; }

    return ops->set(key, val);
}

sw_err_t svc_param_save(void)
{
    const param_store_ops_t *ops = get_ops();

    if (ops == NULL) { return SW_ERR_NOT_INIT; }
    return ops->save();
}
