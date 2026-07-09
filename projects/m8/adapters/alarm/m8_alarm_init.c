/**
 * @file    m8_alarm_init.c
 * @brief   M8 机型报警目录装配实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "projects/m8/adapters/alarm/m8_alarm_init.h"
#include "framework/domain/device_control/mechanism/gantry_alarm_codes.h"
#include "framework/domain/safety/alarm_registry/alarm_registry.h"
#include "framework/ports/inbound/safety/alarm_binding_port.h"
#include "framework/ports/outbound/safety/op_mode_alarm_port.h"
#include "projects/m8/config/m8_alarm_table.h"
#include "projects/m8/config/m8_alarm_comm_table.h"
#include "projects/m8/config/m8_alarm_sw_table.h"
#include "framework/common/log.h"
#include <string.h>

static const alarm_def_t s_di_defs[] = {
#define X(pin, al, tr, rl, cls, idx, nat, lvl, resp, clr, sk, sc, cut, desc) \
    { ALARM_CODE_MAKE(cls, idx, nat), lvl, resp, clr, sk, sc, cut, desc },
    M8_HW_ALARM_TABLE(X)
#undef X
};

static const alarm_def_t s_comm_defs[] = {
#define X(dev, timeout, cls, idx, nat, lvl, resp, clr, cut, desc) \
    { ALARM_CODE_MAKE(cls, idx, nat), lvl, resp, clr, ALARM_SOURCE_COMM, \
      ALARM_SCOPE_NONE, cut, desc },
    M8_COMM_WATCHDOG_TABLE(X)
#undef X
};

static const alarm_def_t s_sw_defs[] = {
#define X(cls, idx, nat, lvl, resp, clr, sk, sc, desc) \
    { ALARM_CODE_MAKE(cls, idx, nat), lvl, resp, clr, sk, sc, false, desc },
    M8_SW_ALARM_TABLE(X)
#undef X
};

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* 编译期校验：framework gantry 流程报警码与 M8 目录一致 */
_Static_assert(M8_GANTRY_ALM_ENC_ERR == GANTRY_ALM_ENC_ERR,
               "gantry enc alarm code mismatch");
_Static_assert(M8_GANTRY_ALM_FWD_TMO == GANTRY_ALM_FWD_TMO,
               "gantry fwd timeout alarm code mismatch");
_Static_assert(M8_GANTRY_ALM_REV_TMO == GANTRY_ALM_REV_TMO,
               "gantry rev timeout alarm code mismatch");

static sw_err_t append_table(alarm_def_t *merged, unsigned *count, unsigned max,
                             const alarm_def_t *table, unsigned table_count)
{
    if ((*count + table_count) > max)
    {
        LOG_ERROR("m8_alarm_init: catalog overflow (have=%u add=%u max=%u)",
                  *count, table_count, max);
        return SW_ERR_OVERFLOW;
    }

    memcpy(&merged[*count], table, table_count * sizeof(merged[0]));
    *count += table_count;
    return SW_OK;
}

sw_err_t m8_alarm_init(void)
{
    const alarm_binding_ops_t *ops = alarm_binding_get_ops();
    alarm_def_t                merged[ALARM_CATALOG_MAX];
    unsigned                   count = 0U;
    unsigned                   max_user_defs = ALARM_CATALOG_MAX - 1U;
    sw_err_t                   r;

    if (ops == NULL)
    {
        LOG_ERROR("m8_alarm_init: alarm_binding_port not registered");
        return SW_ERR_NOT_INIT;
    }

    r = append_table(merged, &count, max_user_defs, s_di_defs, ARRAY_COUNT(s_di_defs));
    if (r != SW_OK)
    {
        return r;
    }
    r = append_table(merged, &count, max_user_defs, s_comm_defs, ARRAY_COUNT(s_comm_defs));
    if (r != SW_OK)
    {
        return r;
    }
    r = append_table(merged, &count, max_user_defs, s_sw_defs, ARRAY_COUNT(s_sw_defs));
    if (r != SW_OK)
    {
        return r;
    }

    r = ops->load_catalog(merged, count);
    if (r != SW_OK)
    {
        LOG_ERROR("m8_alarm_init: load_catalog failed ret=%d (total=%u)", (int)r, count);
        return r;
    }

    alarm_registry_set_estop_skip_fn(op_mode_alarm_port_is_estop);

    LOG_INFO("m8_alarm_init: catalog loaded (di=%u comm=%u sw=%u total=%u)",
             (unsigned)ARRAY_COUNT(s_di_defs),
             (unsigned)ARRAY_COUNT(s_comm_defs),
             (unsigned)ARRAY_COUNT(s_sw_defs),
             count);
    return SW_OK;
}
