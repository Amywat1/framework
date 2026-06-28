/**
 * @file    engine_program_loader_port.c
 * @brief   控制引擎方案加载端口实现（单例注册）
 * @author  huwangwei
 * @date    2026-06-28
 */

#include "ports/storage/engine_program_loader_port.h"

#include <stddef.h>
#include <string.h>

static const engine_program_loader_ops_t *s_ops = NULL;

void engine_program_loader_register(const engine_program_loader_ops_t *ops)
{
    if ((ops != NULL) && (ops->load != NULL))
    {
        s_ops = ops;
    }
}

const engine_program_loader_ops_t *engine_program_loader_get_ops(void)
{
    return s_ops;
}

engine_program_t *engine_program_load(const char *path, char *err, unsigned errsz)
{
    if ((s_ops == NULL) || (s_ops->load == NULL))
    {
        if ((err != NULL) && (errsz > 0U))
        {
            (void)strncpy(err, "engine_program_loader not registered", (size_t)errsz - 1U);
            err[errsz - 1U] = '\0';
        }
        return NULL;
    }
    return s_ops->load(path, err, errsz);
}
