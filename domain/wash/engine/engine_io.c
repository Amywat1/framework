/**
 * @file    engine_io.c
 * @brief   引擎 IO 后端注册单例
 * @author  huwangwei
 * @date    2026-06-25
 */

#include "domain/wash/engine/engine_io.h"

#include <stddef.h>

/* 已注册的后端操作集（s_ 前缀：文件内静态） */
static const engine_io_ops_t     *s_ops     = NULL;
static const engine_io_catalog_t *s_catalog = NULL;

void engine_io_register(const engine_io_backend_t *backend)
{
    if (backend == NULL) {
        return;
    }
    if ((backend->ops == NULL)
        || (backend->ops->read_signal == NULL)
        || (backend->ops->read_axis == NULL)
        || (backend->ops->write_output == NULL)) {
        return;
    }
    s_ops     = backend->ops;
    s_catalog = backend->catalog;
}

const engine_io_ops_t *engine_io_get_ops(void)
{
    return s_ops;
}

const engine_io_catalog_t *engine_io_get_catalog(void)
{
    return s_catalog;
}
