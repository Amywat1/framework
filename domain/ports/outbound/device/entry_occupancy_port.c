/**
 * @file    entry_occupancy_port.c
 * @brief   入场占用口单例注册与查询转发
 */

#include "domain/ports/outbound/device/entry_occupancy_port.h"

#include <stddef.h>

static const entry_occupancy_ops_t *s_ops;

sw_err_t entry_occupancy_register(const entry_occupancy_ops_t *ops)
{
    if ((ops != NULL) && (ops->get == NULL)) {
        return SW_ERR_PARAM;
    }
    s_ops = ops;
    return SW_OK;
}

const entry_occupancy_ops_t *entry_occupancy_get_ops(void)
{
    return s_ops;
}

entry_occupancy_t entry_occupancy_get(void)
{
    entry_occupancy_t view;

    view.available = false;
    view.phase     = ENTRY_OCC_EMPTY;
    if ((s_ops == NULL) || (s_ops->get == NULL)) {
        return view;
    }
    view            = s_ops->get();
    view.available = true;
    return view;
}
