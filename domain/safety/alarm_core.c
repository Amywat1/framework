/**
 * @file    alarm_core.c
 * @brief   报警核心引擎实现
 * @author  HUWANGWEI
 * @date    2026-06-26
 */

#include "domain/safety/alarm_core.h"
#include "ports/safety/alarm_binding_port.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"
#include <pthread.h>
#include <stdbool.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 内置兜底目录（仅安全必需项）
 *   仅在 JSON 目录加载失败时生效，保证安全报警链不失效；不追求覆盖全部报警。
 *   长期应只保留 CRITICAL 类安全报警；普通报警缺失时降级为「不报」是可接受的
 *   故障安全行为，强于「整表为空导致急停也不报」。
 * ------------------------------------------------------------------------- */
static const alarm_def_t s_fallback_defs[] = {
    { ALARM_CODE_MAKE(2, 17, 9), ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, "急停按钮触发(兜底)" },
};

#define ALARM_FALLBACK_COUNT  (sizeof(s_fallback_defs) / sizeof(s_fallback_defs[0]))

/* -------------------------------------------------------------------------
 * 报警目录（运行期可加载，固定容量）+ 活跃集
 *   s_catalog 下标即 s_active 下标；s_count 为当前有效条目数。
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
    int highest = -1; /* 活跃集中的最高等级 */

    pthread_mutex_lock(&s_mutex);
    for (int i = 0; i < (int)s_count; ++i)
    {
        if (s_active[i] && ((int)s_catalog[i].level > highest))
        {
            highest = (int)s_catalog[i].level;
        }
    }
    pthread_mutex_unlock(&s_mutex);

    if (highest == (int)ALARM_LEVEL_CRITICAL)
    {
        return SAFETY_STATE_LOCKOUT;
    }
    if (highest == (int)ALARM_LEVEL_MAJOR)
    {
        return SAFETY_STATE_WARNING;
    }
    return SAFETY_STATE_OK; /* MINOR 或无活跃报警 */
}

uint32_t alarm_core_top_code(void)
{
    int      highest = -1;
    uint32_t code    = ALARM_CODE_NONE;

    pthread_mutex_lock(&s_mutex);
    for (int i = 0; i < (int)s_count; ++i)
    {
        if (s_active[i] && ((int)s_catalog[i].level > highest))
        {
            highest = (int)s_catalog[i].level;
            code    = s_catalog[i].code;
        }
    }
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
    for (unsigned i = 0; i < ALARM_CATALOG_MAX; ++i)
    {
        s_active[i] = false;
    }
    pthread_mutex_unlock(&s_mutex);

    LOG_INFO("alarm_core: catalog loaded, defs=%u", count);
    return SW_OK;
}

sw_err_t alarm_core_init(void)
{
    static const alarm_binding_ops_t s_binding_ops = {
        .trigger = alarm_core_trigger,
        .clear   = alarm_core_clear,
    };

    /* 先装载内置兜底目录，保证任何时刻安全报警链可用；随后 bootstrap 用
     * JSON 目录整表替换。load 同时清空活跃集。*/
    (void)alarm_core_load(s_fallback_defs, (unsigned)ALARM_FALLBACK_COUNT);

    alarm_binding_register(&s_binding_ops);
    LOG_INFO("alarm_core: init ok (兜底目录 defs=%u)", (unsigned)s_count);
    return SW_OK;
}
