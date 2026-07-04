/**
 * @file    m8_alarm_init.c
 * @brief   M8 机型报警目录装配实现
 * @author  HUWANGWEI
 * @date    2026-06-28
 */

#include "projects/m8/adapters/alarm/m8_alarm_init.h"
#include "framework/ports/inbound/safety/alarm_binding_port.h"
#include "framework/domain/safety/model/alarm_code.h"
#include "projects/m8/config/m8_alarm_table.h"
#include "projects/m8/config/m8_alarm_comm_table.h"
#include "projects/m8/config/m8_alarm_sw_table.h"
#include "framework/common/log.h"
#include <string.h>

/* -------------------------------------------------------------------------
 * 各表展开为 alarm_def_t 静态数组
 * ------------------------------------------------------------------------- */

/* 表1：DI 硬件报警（m8_alarm_table.h / M8_HW_ALARM_TABLE）*/
static const alarm_def_t s_di_defs[] = {
#define X(pin, al, tr, rl, cls, idx, nat, lvl, clr, desc) \
    { ALARM_CODE_MAKE((cls), (idx), (nat)), (lvl), (clr), (desc) },
    M8_HW_ALARM_TABLE(X)
#undef X
};

/* 表2：周期通讯设备报警（m8_comm_watchdog_table.h / M8_COMM_WATCHDOG_TABLE）*/
static const alarm_def_t s_comm_defs[] = {
#define X(dev, timeout, cls, idx, nat, lvl, clr, desc) \
    { ALARM_CODE_MAKE((cls), (idx), (nat)), (lvl), (clr), (desc) },
    M8_COMM_WATCHDOG_TABLE(X)
#undef X
};

/* 表3：按需通讯 + 软件逻辑报警（m8_sw_alarm_table.h / M8_SW_ALARM_TABLE）*/
static const alarm_def_t s_sw_defs[] = {
#define X(cls, idx, nat, lvl, clr, desc) \
    { ALARM_CODE_MAKE((cls), (idx), (nat)), (lvl), (clr), (desc) },
    M8_SW_ALARM_TABLE(X)
#undef X
};

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */

sw_err_t m8_alarm_init(void)
{
    const alarm_binding_ops_t *ops = alarm_binding_get_ops();
    if (ops == NULL)
    {
        LOG_ERROR("m8_alarm_init: alarm_binding_port not registered");
        return SW_ERR_NOT_INIT;
    }

    /* 合并三张表到栈上临时数组，一次 load_catalog */
    alarm_def_t merged[ALARM_CATALOG_MAX];
    unsigned    count = 0U;

    for (unsigned i = 0; i < ARRAY_COUNT(s_di_defs); ++i)
    {
        merged[count++] = s_di_defs[i];
    }
    for (unsigned i = 0; i < ARRAY_COUNT(s_comm_defs); ++i)
    {
        merged[count++] = s_comm_defs[i];
    }
    for (unsigned i = 0; i < ARRAY_COUNT(s_sw_defs); ++i)
    {
        merged[count++] = s_sw_defs[i];
    }

    /* load_catalog 会在溢出时返回 SW_ERR_OVERFLOW */
    sw_err_t r = ops->load_catalog(merged, count);
    if (r != SW_OK)
    {
        LOG_ERROR("m8_alarm_init: load_catalog failed ret=%d (total=%u)", (int)r, count);
        return r;
    }

    LOG_INFO("m8_alarm_init: catalog loaded (di=%u comm=%u sw=%u total=%u)",
             (unsigned)ARRAY_COUNT(s_di_defs),
             (unsigned)ARRAY_COUNT(s_comm_defs),
             (unsigned)ARRAY_COUNT(s_sw_defs),
             count);
    return SW_OK;
}
