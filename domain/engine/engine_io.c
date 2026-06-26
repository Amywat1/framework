/**
 * @file    engine_io.c
 * @brief   引擎 IO 后端注册单例
 * @author  huwangwei
 * @date    2026-06-25
 */

#include "domain/engine/engine_io.h"

#include <stddef.h>

/* 已注册的后端操作集（s_ 前缀：文件内静态） */
static const engine_io_ops_t *s_ops = NULL;

void engine_io_register(const engine_io_ops_t *ops)
{
    if (ops == NULL)
    {
        return;
    }
    if ((ops->read_signal == NULL) || (ops->read_axis == NULL) ||
        (ops->write_output == NULL))
    {
        return;
    }
    s_ops = ops;
}

const engine_io_ops_t *engine_io_get_ops(void)
{
    return s_ops;
}
