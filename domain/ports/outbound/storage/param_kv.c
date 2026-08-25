/**
 * @file    param_kv.c
 * @brief   运行参数具名访问实现（经 param_store_ops 读写）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    不直接操作存储介质。底层读写委托给 param_store_ops
 *          （由 wiring 注册 json_param_store 等实现）。
 *          整型参数以字符串形式存入 param_store，读取时做 strtol 转换。
 *          若 param_store 未注册，所有读取返回默认值，写入/保存静默失败。
 */

#include "domain/ports/outbound/storage/param_kv.h"

#include "common/log.h"
#include "domain/ports/outbound/storage/param_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 内部辅助：获取 ops 并检查非空
 * ------------------------------------------------------------------------- */
static const param_store_ops_t *get_ops(void)
{
    const param_store_ops_t *ops = param_store_get_ops();
    if (ops == NULL) {
        LOG_WARN("param_kv: param_store not registered");
    }
    return ops;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t param_kv_init(void)
{
    const param_store_ops_t *ops = get_ops();
    if (ops == NULL) {
        return SW_ERR_NOT_INIT;
    }
    return ops->load();
}

int param_kv_get_int(const char *key, int default_val)
{
    char                     buf[32];
    char                    *end;
    long                     val;
    const param_store_ops_t *ops = param_store_get_ops();

    if ((ops == NULL) || (key == NULL)) {
        return default_val;
    }
    if (ops->get(key, buf, sizeof(buf)) != SW_OK) {
        return default_val;
    }
    if (buf[0] == '\0') {
        return default_val;
    }
    val = strtol(buf, &end, 10);
    if ((end == buf) || (*end != '\0')) {
        return default_val;
    }
    return (int)val;
}

sw_err_t param_kv_get_str(const char *key, char *buf, int buf_size, const char *default_val)
{
    const param_store_ops_t *ops = param_store_get_ops();

    if ((buf == NULL) || (buf_size <= 0)) {
        return SW_ERR_PARAM;
    }

    if ((ops == NULL) || (key == NULL) || (ops->get(key, buf, (size_t)buf_size) != SW_OK)) {
        /* 未注册或键不存在：写入默认值 */
        strncpy(buf, (default_val != NULL) ? default_val : "", (size_t)(buf_size - 1));
        buf[buf_size - 1] = '\0';
    }
    return SW_OK;
}

sw_err_t param_kv_set_int(const char *key, int val)
{
    char                     buf[32];
    const param_store_ops_t *ops = get_ops();

    if (ops == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (key == NULL) {
        return SW_ERR_PARAM;
    }

    snprintf(buf, sizeof(buf), "%d", val);
    return ops->set(key, buf);
}

sw_err_t param_kv_set_str(const char *key, const char *val)
{
    const param_store_ops_t *ops = get_ops();

    if (ops == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if ((key == NULL) || (val == NULL)) {
        return SW_ERR_PARAM;
    }

    return ops->set(key, val);
}

sw_err_t param_kv_save(void)
{
    const param_store_ops_t *ops = get_ops();

    if (ops == NULL) {
        return SW_ERR_NOT_INIT;
    }
    return ops->save();
}
