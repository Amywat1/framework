/**
 * @file    alarm_core.c
 * @brief   报警核心引擎实现
 * @author  HUWANGWEI
 * @date    2026-06-26
 */

#include "framework/domain/safety/alarm/alarm_core.h"
#include "framework/ports/inbound/safety/alarm_binding_port.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"
#include <pthread.h>
#include <stdbool.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 报警目录（运行期可加载，固定容量）+ 活跃集
 *   s_catalog 下标即 s_active 下标；s_count 为当前有效条目数。
 *   s_catalog 仅在 alarm_core_load()（init 阶段，poll 前）写入；
 *   poll 期间只读，因此 find_index() / 清除方式检查可在锁外安全访问。
 * ------------------------------------------------------------------------- */
static alarm_def_t     s_catalog[ALARM_CATALOG_MAX];
static unsigned        s_count;
static bool            s_active[ALARM_CATALOG_MAX];
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

/* -------------------------------------------------------------------------
 * 内部辅助
 * ------------------------------------------------------------------------- */

/** @brief  按报警码查定义表下标；未找到返回 -1 */
static int find_index(uint32_t alarm_code)
{
    for (int i = 0; i < (int)s_count; ++i)
    {
        if (s_catalog[i].code == alarm_code)
        {
            return i;
        }
    }
    return -1;
}

/** @brief  设置活跃状态并在状态翻转时返回 true（持锁内调用）*/
static bool set_active_locked(int idx, bool active)
{
    if (s_active[idx] == active)
    {
        return false;
    }
    s_active[idx] = active;
    return true;
}

/**
 * @brief  在持锁环境下扫描活跃集，输出最高等级和对应报警码
 * @param  out_level  输出最高等级（无活跃报警时为 -1），可传 NULL
 * @param  out_code   输出最高等级报警码（无活跃报警时为 ALARM_CODE_NONE），可传 NULL
 */
static void scan_active_locked(int *out_level, uint32_t *out_code)
{
    int      highest = -1;
    uint32_t code    = ALARM_CODE_NONE;

    for (int i = 0; i < (int)s_count; ++i)
    {
        if (s_active[i] && ((int)s_catalog[i].level > highest))
        {
            highest = (int)s_catalog[i].level;
            code    = s_catalog[i].code;
        }
    }
    if (out_level != NULL) { *out_level = highest; }
    if (out_code  != NULL) { *out_code  = code;    }
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */

sw_err_t alarm_core_trigger(uint32_t alarm_code)
{
    int  idx = find_index(alarm_code);
    bool changed;

    if (idx < 0)
    {
        LOG_WARN("alarm_core: trigger unknown code=%06u", (unsigned)alarm_code);
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    changed = set_active_locked(idx, true);
    pthread_mutex_unlock(&s_mutex);

    if (changed)
    {
        LOG_WARN("alarm_core: TRIGGERED %06u (%s)",
                 (unsigned)alarm_code, s_catalog[idx].desc);
        (void)event_publish(EVT_ALARM_TRIGGERED, alarm_code);
    }
    return SW_OK;
}

sw_err_t alarm_core_clear(uint32_t alarm_code)
{
    int  idx = find_index(alarm_code);
    bool changed;

    if (idx < 0)
    {
        LOG_WARN("alarm_core: clear unknown code=%06u", (unsigned)alarm_code);
        return SW_ERR_PARAM;
    }

    /* 锁存报警忽略自动清除请求；须经 CMD_RESET_FAULT → alarm_core_reset_alarms() 人工复位 */
    if (s_catalog[idx].clear == ALARM_CLEAR_LATCHED)
    {
        return SW_OK;
    }

    pthread_mutex_lock(&s_mutex);
    changed = set_active_locked(idx, false);
    pthread_mutex_unlock(&s_mutex);

    if (changed)
    {
        LOG_INFO("alarm_core: CLEARED %06u (%s)",
                 (unsigned)alarm_code, s_catalog[idx].desc);
        (void)event_publish(EVT_ALARM_CLEARED, alarm_code);
    }
    return SW_OK;
}

safety_state_t alarm_core_safety_state(void)
{
    int highest;

    pthread_mutex_lock(&s_mutex);
    scan_active_locked(&highest, NULL);
    pthread_mutex_unlock(&s_mutex);

    if (highest == (int)ALARM_LEVEL_CRITICAL) { return SAFETY_STATE_LOCKOUT; }
    if (highest == (int)ALARM_LEVEL_MAJOR)    { return SAFETY_STATE_WARNING;  }
    return SAFETY_STATE_OK; /* MINOR 或无活跃报警 */
}

uint32_t alarm_core_top_code(void)
{
    uint32_t code;

    pthread_mutex_lock(&s_mutex);
    scan_active_locked(NULL, &code);
    pthread_mutex_unlock(&s_mutex);

    return code;
}

sw_err_t alarm_core_load(const alarm_def_t *defs, unsigned count)
{
    if (defs == NULL)
    {
        return SW_ERR_PARAM;
    }
    if (count > ALARM_CATALOG_MAX)
    {
        LOG_ERROR("alarm_core: catalog too large (%u > %u)",
                  count, (unsigned)ALARM_CATALOG_MAX);
        return SW_ERR_OVERFLOW;
    }

    pthread_mutex_lock(&s_mutex);
    memcpy(s_catalog, defs, (size_t)count * sizeof(s_catalog[0]));
    s_count = count;
    memset(s_active, 0, sizeof(s_active));
    pthread_mutex_unlock(&s_mutex);

    LOG_INFO("alarm_core: catalog loaded, defs=%u", count);
    return SW_OK;
}

sw_err_t alarm_core_reset_alarms(void)
{
    uint32_t codes[ALARM_CATALOG_MAX];
    unsigned cleared = 0U;

    pthread_mutex_lock(&s_mutex);
    for (unsigned i = 0; i < s_count; ++i)
    {
        if (s_active[i])
        {
            codes[cleared++] = s_catalog[i].code;
            s_active[i]      = false;
        }
    }
    pthread_mutex_unlock(&s_mutex);

    for (unsigned i = 0; i < cleared; ++i)
    {
        LOG_INFO("alarm_core: RESET %06u", (unsigned)codes[i]);
    }
    if (cleared > 0U)
    {
        /* 单次广播触发 safety_fsm 和 safety_supervisor 重新聚合安全态 */
        (void)event_publish(EVT_ALARM_CLEARED, ALARM_CODE_NONE);
    }
    return SW_OK;
}

sw_err_t alarm_core_init(void)
{
    static const alarm_binding_ops_t s_binding_ops = {
        .trigger      = alarm_core_trigger,
        .clear        = alarm_core_clear,
        .load_catalog = alarm_core_load,
    };

    /* 目录由机型适配器（m8_alarm_adapt_init）通过端口 load_catalog 注入；
     * 注入前目录为空，trigger/clear 对任何码均返回 SW_ERR_PARAM（安全行为）。*/
    alarm_binding_register(&s_binding_ops);
    LOG_INFO("alarm_core: init ok");
    return SW_OK;
}
