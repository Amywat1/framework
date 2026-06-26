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

/* -------------------------------------------------------------------------
 * 报警定义表（静态配置，示例集）
 * 行下标即活跃集 s_active[] 的下标；新增报警在此追加即可。
 * ------------------------------------------------------------------------- */
static const alarm_def_t s_alarm_defs[] = {
    { ALARM_CODE_ESTOP,              ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, "急停按钮触发" },
    { ALARM_CODE_SIDE_BRUSH_OVERLOAD, ALARM_LEVEL_MAJOR,   ALARM_CLEAR_AUTO_STATIC, "侧刷电机过载" },
    { ALARM_CODE_FAN_FAULT,          ALARM_LEVEL_MINOR,    ALARM_CLEAR_AUTO_STATIC, "风机报警反馈" },
};

#define ALARM_DEF_COUNT  (sizeof(s_alarm_defs) / sizeof(s_alarm_defs[0]))

static bool            s_active[ALARM_DEF_COUNT];
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

/* -------------------------------------------------------------------------
 * 内部辅助
 * ------------------------------------------------------------------------- */

/** @brief  按报警码查定义表下标；未找到返回 -1 */
static int find_index(uint32_t alarm_code)
{
    for (int i = 0; i < (int)ALARM_DEF_COUNT; ++i)
    {
        if (s_alarm_defs[i].code == alarm_code)
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
        LOG_WARN("alarm_core: trigger unknown code=0x%06X", (unsigned)alarm_code);
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    changed = set_active_locked(idx, true);
    pthread_mutex_unlock(&s_mutex);

    if (changed)
    {
        LOG_WARN("alarm_core: TRIGGERED 0x%06X (%s)",
                 (unsigned)alarm_code, s_alarm_defs[idx].desc);
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
        LOG_WARN("alarm_core: clear unknown code=0x%06X", (unsigned)alarm_code);
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    changed = set_active_locked(idx, false);
    pthread_mutex_unlock(&s_mutex);

    if (changed)
    {
        LOG_INFO("alarm_core: CLEARED 0x%06X (%s)",
                 (unsigned)alarm_code, s_alarm_defs[idx].desc);
        (void)event_publish(EVT_ALARM_CLEARED, alarm_code);
    }
    return SW_OK;
}

safety_state_t alarm_core_safety_state(void)
{
    int highest = -1; /* 活跃集中的最高等级 */

    pthread_mutex_lock(&s_mutex);
    for (int i = 0; i < (int)ALARM_DEF_COUNT; ++i)
    {
        if (s_active[i] && ((int)s_alarm_defs[i].level > highest))
        {
            highest = (int)s_alarm_defs[i].level;
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
    for (int i = 0; i < (int)ALARM_DEF_COUNT; ++i)
    {
        if (s_active[i] && ((int)s_alarm_defs[i].level > highest))
        {
            highest = (int)s_alarm_defs[i].level;
            code    = s_alarm_defs[i].code;
        }
    }
    pthread_mutex_unlock(&s_mutex);

    return code;
}

sw_err_t alarm_core_init(void)
{
    static const alarm_binding_ops_t s_binding_ops = {
        .trigger = alarm_core_trigger,
        .clear   = alarm_core_clear,
    };

    pthread_mutex_lock(&s_mutex);
    for (int i = 0; i < (int)ALARM_DEF_COUNT; ++i)
    {
        s_active[i] = false;
    }
    pthread_mutex_unlock(&s_mutex);

    alarm_binding_register(&s_binding_ops);
    LOG_INFO("alarm_core: init ok, defs=%d", (int)ALARM_DEF_COUNT);
    return SW_OK;
}
