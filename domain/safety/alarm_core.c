/**
 * @file    alarm_core.c
 * @brief   报警核心引擎实现
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    报警表编译期内嵌在本文件中。
 *          机型相关的触发采集逻辑通过回调注入。
 */

#include "domain/safety/alarm_core.h"
#include "domain/model/alarm_code.h"
#include "config/machine/m8_machine_config.h"
#include "core/event_bus/event_bus.h"
#include "common/log.h"
#include <string.h>
#include <pthread.h>

/* -------------------------------------------------------------------------
 * 报警表（编译期只读）
 * 首行必须是急停（ALARM_CODE_ESTOP），trigger_ms=0 保证立即激活
 * ------------------------------------------------------------------------- */
static const alarm_entry_t s_table[] = {
    /* 安全类 */
    { ALARM_CODE_ESTOP,          0,   ALARM_LEVEL_ERROR,   0,    ALARM_RECOVER_MANUAL,                       "急停" },
    /* 限位异常 */
    { ALARM_CODE_GANTRY_FWD_LIM, 500, ALARM_LEVEL_ERROR,   500,  ALARM_RECOVER_AUTO,                         "龙门前限位异常" },
    { ALARM_CODE_GANTRY_REV_LIM, 500, ALARM_LEVEL_ERROR,   500,  ALARM_RECOVER_AUTO,                         "龙门后限位异常" },
    { ALARM_CODE_LIFT_UP_LIM,    500, ALARM_LEVEL_ERROR,   500,  ALARM_RECOVER_MANUAL,                       "顶刷升降上限位异常" },
    { ALARM_CODE_LIFT_DOWN_LIM,  500, ALARM_LEVEL_ERROR,   500,  ALARM_RECOVER_MANUAL,                       "顶刷升降下限位异常" },
    /* VFD / 步进驱动故障 */
    { ALARM_CODE_VFD_GANTRY,     200, ALARM_LEVEL_ERROR,   1000, ALARM_RECOVER_DRIVE | ALARM_RECOVER_MANUAL, "龙门VFD故障" },
    { ALARM_CODE_VFD_BRUSH,      200, ALARM_LEVEL_ERROR,   1000, ALARM_RECOVER_DRIVE | ALARM_RECOVER_MANUAL, "刷子VFD故障" },
    { ALARM_CODE_STEPPER_FAULT,  200, ALARM_LEVEL_ERROR,   1000, ALARM_RECOVER_DRIVE | ALARM_RECOVER_MANUAL, "步进驱动器故障" },
    /* 通信丢失 */
    { ALARM_CODE_MODBUS_GANTRY,  0,   ALARM_LEVEL_WARNING, 2000, ALARM_RECOVER_AUTO,                         "龙门Modbus通信丢失" },
    { ALARM_CODE_MODBUS_BRUSH,   0,   ALARM_LEVEL_WARNING, 2000, ALARM_RECOVER_AUTO,                         "刷子Modbus通信丢失" },
    /* 电流异常 */
    { ALARM_CODE_BRUSH_CURRENT,  0,   ALARM_LEVEL_ERROR,   1000, ALARM_RECOVER_DRIVE | ALARM_RECOVER_MANUAL, "刷子电流异常" },
    { ALARM_CODE_GANTRY_CURRENT, 0,   ALARM_LEVEL_ERROR,   1000, ALARM_RECOVER_DRIVE | ALARM_RECOVER_MANUAL, "龙门电流异常" },
    /* 水系统 */
    { ALARM_CODE_PUMP_DRY_RUN,   (CFG_WATER_PUMP_DRY_RUN_TIMEOUT_S * 1000U),
                                  ALARM_LEVEL_WARNING, 1000, ALARM_RECOVER_AUTO,                         "水泵空转保护" },
    /* 云端 */
    { ALARM_CODE_MQTT_OFFLINE,   0,   ALARM_LEVEL_NOTICE,  0,    ALARM_RECOVER_AUTO,                         "云端MQTT断线" },
};

#define ALARM_TABLE_SIZE    ((int)(sizeof(s_table) / sizeof(s_table[0])))

/* -------------------------------------------------------------------------
 * 运行时状态（与配置表按下标一一对应）
 * ------------------------------------------------------------------------- */
typedef struct
{
    bool  raw_triggered;        /* 当前原始触发状态（IO轮询/驱动事件更新）*/
    bool  active;               /* 报警已激活（经过防抖后置位）*/
    int   debounce_elapsed_ms;  /* 触发防抖计时（ms）*/
    int   recover_elapsed_ms;   /* 恢复防抖计时（ms）*/
    bool  just_notice;          /* true = 强制降级为 NOTICE（硬件未安装场景）*/
} alarm_rt_t;

static alarm_rt_t       s_rt[ALARM_TABLE_SIZE];
static pthread_mutex_t  s_mutex       = PTHREAD_MUTEX_INITIALIZER;
static alarm_poll_fn_t       s_poll_fn      = NULL;
static alarm_emc_reset_fn_t  s_emc_reset_fn = NULL;

/* -------------------------------------------------------------------------
 * 内部辅助
 * ------------------------------------------------------------------------- */
static int find_idx(uint16_t code)
{
    int i;
    for (i = 0; i < ALARM_TABLE_SIZE; i++)
    {
        if (s_table[i].code == code)
        {
            return i;
        }
    }
    return -1;
}

/* 获取有效等级（考虑 just_notice 降级）*/
static alarm_level_t get_effective_level(int idx)
{
    return s_rt[idx].just_notice ? ALARM_LEVEL_NOTICE : s_table[idx].level;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t alarm_core_init(void)
{
    pthread_mutex_lock(&s_mutex);
    memset(s_rt, 0, sizeof(s_rt));
    pthread_mutex_unlock(&s_mutex);

    /* 清零回调，防止单测间状态串漏 */
    s_poll_fn      = NULL;
    s_emc_reset_fn = NULL;

    LOG_INFO("alarm_core: init ok, %d alarms configured", ALARM_TABLE_SIZE);
    return SW_OK;
}

void alarm_core_register_poll_fn(alarm_poll_fn_t fn)
{
    s_poll_fn = fn;
}

void alarm_core_register_emc_reset_fn(alarm_emc_reset_fn_t fn)
{
    s_emc_reset_fn = fn;
}

void alarm_core_set_raw_trigger(uint16_t code, bool triggered, bool just_notice)
{
    int idx = find_idx(code);
    if (idx < 0)
    {
        LOG_WARN("alarm_core_set_raw_trigger: unknown code %u", (unsigned)code);
        return;
    }
    pthread_mutex_lock(&s_mutex);
    s_rt[idx].raw_triggered = triggered;
    s_rt[idx].just_notice   = just_notice;
    pthread_mutex_unlock(&s_mutex);
}

void alarm_core_set_state(uint16_t code, bool active, bool just_notice)
{
    int           idx           = find_idx(code);
    bool          triggered_evt = false;
    bool          cleared_evt   = false;
    alarm_level_t lvl           = ALARM_LEVEL_NOTICE;

    if (idx < 0)
    {
        LOG_WARN("alarm_core_set_state: unknown code %u", (unsigned)code);
        return;
    }

    pthread_mutex_lock(&s_mutex);
    s_rt[idx].raw_triggered = active;
    s_rt[idx].just_notice   = just_notice;

    if (active && !s_rt[idx].active)
    {
        s_rt[idx].active              = true;
        s_rt[idx].debounce_elapsed_ms = 0;
        s_rt[idx].recover_elapsed_ms  = 0;
        triggered_evt = true;
        lvl           = get_effective_level(idx);
    }
    else if (!active && s_rt[idx].active)
    {
        if (s_table[idx].recover & ALARM_RECOVER_DRIVE)
        {
            /* 驱动直报清除：故障条件已由硬件/驱动确认消失 */
            s_rt[idx].active              = false;
            s_rt[idx].debounce_elapsed_ms = 0;
            s_rt[idx].recover_elapsed_ms  = 0;
            cleared_evt = true;
        }
        else if (!(s_table[idx].recover & ALARM_RECOVER_MANUAL))
        {
            s_rt[idx].recover_elapsed_ms = 0;
        }
    }
    pthread_mutex_unlock(&s_mutex);

    if (triggered_evt)
    {
        (void)event_publish(EVT_ALARM_TRIGGERED, (uint32_t)code);
        LOG_WARN("alarm_core: TRIGGERED code=%u [%s] level=%d",
                 (unsigned)code, s_table[idx].desc, (int)lvl);
    }
    else if (cleared_evt)
    {
        (void)event_publish(EVT_ALARM_CLEARED, (uint32_t)code);
        LOG_INFO("alarm_core: CLEARED code=%u [%s] (drive direct)",
                 (unsigned)code, s_table[idx].desc);
    }
}

void alarm_core_tick_ms(int elapsed_ms)
{
    int      i;
    uint16_t triggered_codes[ALARM_TABLE_SIZE];
    uint16_t cleared_codes[ALARM_TABLE_SIZE];
    int      triggered_count = 0;
    int      cleared_count   = 0;

    /* ① 调用 IO 信号轮询回调（不持锁，poll_fn 会调用 set_raw_trigger）*/
    if (s_poll_fn != NULL)
    {
        s_poll_fn();
    }

    /* ② 防抖计时 + 恢复计时 */
    pthread_mutex_lock(&s_mutex);
    for (i = 0; i < ALARM_TABLE_SIZE; i++)
    {
        alarm_rt_t         *rt  = &s_rt[i];
        const alarm_entry_t *cfg = &s_table[i];

        if (!rt->active)
        {
            /* 未激活：计数触发防抖 */
            if (rt->raw_triggered)
            {
                rt->debounce_elapsed_ms += elapsed_ms;
                if (rt->debounce_elapsed_ms >= cfg->trigger_ms)
                {
                    rt->active              = true;
                    rt->debounce_elapsed_ms = 0;
                    rt->recover_elapsed_ms  = 0;
                    triggered_codes[triggered_count++] = cfg->code;
                }
            }
            else
            {
                rt->debounce_elapsed_ms = 0;
            }
        }
        else
        {
            /* 已激活：检查自动恢复 */
            if (cfg->recover & ALARM_RECOVER_MANUAL)
            {
                /* 手动恢复：tick 不处理，等待 manual_reset() */
            }
            else if (!rt->raw_triggered)
            {
                /* AUTO / DRIVE（无 MANUAL）：恢复防抖计时 */
                rt->recover_elapsed_ms += elapsed_ms;
                if (rt->recover_elapsed_ms >= cfg->recover_ms)
                {
                    rt->active             = false;
                    rt->recover_elapsed_ms = 0;
                    cleared_codes[cleared_count++] = cfg->code;
                }
            }
            else
            {
                /* 仍在触发：重置恢复计时 */
                rt->recover_elapsed_ms = 0;
            }
        }
    }
    pthread_mutex_unlock(&s_mutex);

    /* ③ 发布事件（锁外执行，避免持锁时调用 event_bus）*/
    for (i = 0; i < triggered_count; i++)
    {
        int idx = find_idx(triggered_codes[i]);
        alarm_level_t lvl = (idx >= 0) ? get_effective_level(idx) : ALARM_LEVEL_NOTICE;
        (void)event_publish(EVT_ALARM_TRIGGERED, (uint32_t)triggered_codes[i]);
        LOG_WARN("alarm_core: TRIGGERED code=%u level=%d",
                 (unsigned)triggered_codes[i], (int)lvl);
    }
    for (i = 0; i < cleared_count; i++)
    {
        (void)event_publish(EVT_ALARM_CLEARED, (uint32_t)cleared_codes[i]);
        LOG_INFO("alarm_core: CLEARED code=%u", (unsigned)cleared_codes[i]);
    }
}

bool alarm_core_is_active(uint16_t code)
{
    bool result;
    int  idx = find_idx(code);
    if (idx < 0)
    {
        return false;
    }
    pthread_mutex_lock(&s_mutex);
    result = s_rt[idx].active;
    pthread_mutex_unlock(&s_mutex);
    return result;
}

bool alarm_core_has_error(void)
{
    bool result = false;
    int  i;
    pthread_mutex_lock(&s_mutex);
    for (i = 0; i < ALARM_TABLE_SIZE; i++)
    {
        if (s_rt[i].active && (get_effective_level(i) == ALARM_LEVEL_ERROR))
        {
            result = true;
            break;
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return result;
}

bool alarm_core_has_warning(void)
{
    bool result = false;
    int  i;
    pthread_mutex_lock(&s_mutex);
    for (i = 0; i < ALARM_TABLE_SIZE; i++)
    {
        if (s_rt[i].active && (get_effective_level(i) == ALARM_LEVEL_WARNING))
        {
            result = true;
            break;
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return result;
}

void alarm_core_manual_reset(void)
{
    int      i;
    uint16_t cleared_codes[ALARM_TABLE_SIZE];
    int      cleared_count = 0;

    /* ① 先执行急停复位动作序列（停机、关水、复位驱动）*/
    if (s_emc_reset_fn != NULL)
    {
        s_emc_reset_fn();
    }

    /* ② 清除所有 MANUAL 恢复报警（前提：触发条件已消失）*/
    pthread_mutex_lock(&s_mutex);
    for (i = 0; i < ALARM_TABLE_SIZE; i++)
    {
        if (s_rt[i].active && (s_table[i].recover & ALARM_RECOVER_MANUAL))
        {
            if (!s_rt[i].raw_triggered)
            {
                /* 触发条件已消失，允许手动清除 */
                s_rt[i].active              = false;
                s_rt[i].debounce_elapsed_ms = 0;
                s_rt[i].recover_elapsed_ms  = 0;
                cleared_codes[cleared_count++] = s_table[i].code;
            }
            else
            {
                /* 条件仍在（如急停未松开），不允许清除 */
                LOG_WARN("alarm_core: manual_reset: code=%u trigger still active, skip",
                         (unsigned)s_table[i].code);
            }
        }
    }
    pthread_mutex_unlock(&s_mutex);

    for (i = 0; i < cleared_count; i++)
    {
        (void)event_publish(EVT_ALARM_CLEARED, (uint32_t)cleared_codes[i]);
        LOG_INFO("alarm_core: MANUAL_RESET cleared code=%u", (unsigned)cleared_codes[i]);
    }
}
