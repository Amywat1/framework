/**
 * @file    engine_actuator.c
 * @brief   引擎执行机构后端注册单例
 * @author  huwangwei
 * @date    2026-07-17
 */

#include "domain/program_engine/engine/engine_actuator.h"

#include <stddef.h>

static const engine_actuator_ops_t     *s_ops     = NULL;
static const engine_actuator_catalog_t *s_catalog = NULL;

void engine_actuator_register(const engine_actuator_backend_t *backend)
{
    if (backend == NULL) {
        return;
    }
    if ((backend->ops == NULL) || (backend->ops->apply == NULL) || (backend->ops->release == NULL)
        || (backend->ops->halt_all == NULL)) {
        return;
    }
    s_ops     = backend->ops;
    s_catalog = backend->catalog;
}

const engine_actuator_ops_t *engine_actuator_get_ops(void)
{
    return s_ops;
}

const engine_actuator_catalog_t *engine_actuator_get_catalog(void)
{
    return s_catalog;
}
