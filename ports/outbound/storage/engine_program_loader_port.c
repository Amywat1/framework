/**
 * @file    engine_program_loader_port.c
 * @brief   控制引擎方案加载端口实现（单例注册）
 * @author  huwangwei
 * @date    2026-06-28
 */

#include "ports/outbound/storage/engine_program_loader_port.h"

#include <stddef.h>
#include <string.h>

static const engine_program_loader_ops_t *s_ops = NULL;

sw_err_t engine_program_loader_register(const engine_program_loader_ops_t *ops)
{
    /* 原实现校验失败后静默忽略，调用方无从得知；现按统一语义返回错误。
     * load 是本端口唯一入口，缺失即不可用。 */
    if ((ops != NULL) && (ops->load == NULL)) {
        return SW_ERR_PARAM;
    }
    s_ops = ops;
    return SW_OK;
}

const engine_program_loader_ops_t *engine_program_loader_get_ops(void)
{
    return s_ops;
}

engine_program_t *engine_program_load(const char *path, char *err, unsigned errsz)
{
    if ((s_ops == NULL) || (s_ops->load == NULL)) {
        if ((err != NULL) && (errsz > 0U)) {
            (void)strncpy(err, "engine_program_loader not registered", (size_t)errsz - 1U);
            err[errsz - 1U] = '\0';
        }
        return NULL;
    }
    return s_ops->load(path, err, errsz);
}

sw_err_t engine_program_verify_integrity(const char *path, char *err, unsigned errsz)
{
    if (s_ops == NULL) {
        if ((err != NULL) && (errsz > 0U)) {
            (void)strncpy(err, "engine_program_loader not registered", (size_t)errsz - 1U);
            err[errsz - 1U] = '\0';
        }
        return SW_ERR_NOT_INIT;
    }

    /* 未提供校验能力时按跳过处理：完整性校验是可选增强，
     * 缺失不应让不带摘要资产的项目无法加载方案。 */
    if (s_ops->verify_integrity == NULL) {
        return SW_OK;
    }

    return s_ops->verify_integrity(path, err, errsz);
}
