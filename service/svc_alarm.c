/**
 * @file    svc_alarm.c
 * @brief   通用报警引擎实现
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#include "svc_alarm.h"
#include "common/log.h"
#include "config/alarm_config.h"
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * 每条报警的运行时状态
 * ------------------------------------------------------------------------- */
typedef struct
{
    bool     is_active;         /* 当前是否处于激活状态 */
    bool     raw_triggered;     /* IO 轮询原始触发标志 */
    bool     just_notice;       /* 是否强制降级为 NOTICE */
    int      trigger_cnt_ms;    /* 触发累计时间（ms）*/
    int      recover_cnt_ms;    /* 恢复累计时间（ms）*/
} AlarmRunState_t;

/* -------------------------------------------------------------------------
 * 模块内部状态
 * ------------------------------------------------------------------------- */
static AlarmRunState_t   s_states[32]      = {0};    /* 最多支持 32 条报警 */
static int               s_alarm_count     = 0;
static SvcAlarmFlags_t   s_flags           = {0};
static pthread_mutex_t   s_mutex           = PTHREAD_MUTEX_INITIALIZER;
static pthread_t         s_poll_thread;
static bool              s_thread_running  = false;

static AlarmSignalPollFn s_poll_fn      = NULL;
static AlarmEmcResetFn   s_emc_reset_fn = NULL;
static AlarmVfdReadFn    s_vfd_read_fn  = NULL;

/* -------------------------------------------------------------------------
 * 内部辅助：根据 code 查找表索引
 * ------------------------------------------------------------------------- */
static int find_entry(uint16_t code)
{
    int i;
    for (i = 0; i < s_alarm_count; i++) {
        if (g_alarm_table[i].code == code) {
            return i;
        }
    }
    return -1;
}

/* -------------------------------------------------------------------------
 * 报警轮询线程（每 10ms 执行一次）
 * ------------------------------------------------------------------------- */
static void *alarm_poll_thread(void *arg)
{
    int      i;
    uint16_t vfd_code = 0U;

    (void)arg;

    while (s_thread_running) {
        usleep(10U * 1000U);    /* 10ms 周期 */

        if (s_flags.is_machine_close) {
            continue;
        }

        pthread_mutex_lock(&s_mutex);

        /* ① 调用 bsp_alarm 注入的 IO 信号轮询函数 */
        if (s_poll_fn != NULL) {
            s_poll_fn();
        }

        /* ② 逐条处理 IO 轮询类报警的防抖计时 */
        for (i = 0; i < s_alarm_count; i++) {
            AlarmRunState_t      *rs = &s_states[i];
            const AlarmEntry_t   *ce = &g_alarm_table[i];

            if (!rs->is_active) {
                if (rs->raw_triggered) {
                    rs->trigger_cnt_ms += 10;
                    if (rs->trigger_cnt_ms >= ce->trigger_ms) {
                        rs->is_active      = true;
                        rs->trigger_cnt_ms = 0;
                        LOG_WARN("ALARM ACTIVE code=%u [%s]",
                                 (unsigned)ce->code, ce->desc);
                    }
                } else {
                    rs->trigger_cnt_ms = 0;
                }
            } else {
                /* 已激活：检查 AUTO 恢复 */
                if ((ce->recover & ALARM_RECOVER_AUTO) &&
                    !rs->raw_triggered) {
                    rs->recover_cnt_ms += 10;
                    if (rs->recover_cnt_ms >= ce->recover_ms) {
                        rs->is_active      = false;
                        rs->recover_cnt_ms = 0;
                        LOG_INFO("ALARM CLEARED code=%u [%s]",
                                 (unsigned)ce->code, ce->desc);
                    }
                } else if (rs->raw_triggered) {
                    rs->recover_cnt_ms = 0;
                }
            }
        }

        /* ③ 读取 VFD 故障码 */
        if (s_vfd_read_fn != NULL) {
            (void)s_vfd_read_fn(&vfd_code);
        }

        pthread_mutex_unlock(&s_mutex);
    }

    return NULL;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t svc_alarm_init(void)
{
    memset(s_states, 0, sizeof(s_states));
    memset(&s_flags, 0, sizeof(s_flags));

    s_alarm_count = (int)ALARM_TABLE_SIZE;
    if (s_alarm_count > 32) {
        LOG_ERROR("svc_alarm: alarm table too large (%d > 32)", s_alarm_count);
        s_alarm_count = 32;
    }

    /* 从 machine_config 初始化硬件安装标志 */
    s_flags.is_brush_top_installed  = (bool)CFG_BRUSH_TOP_INSTALLED;
    s_flags.is_brush_side_installed = (bool)CFG_BRUSH_SIDE_INSTALLED;
    s_flags.is_top_lift_installed   = (bool)CFG_TOP_LIFT_INSTALLED;

    s_thread_running = true;
    if (pthread_create(&s_poll_thread, NULL, alarm_poll_thread, NULL) != 0) {
        LOG_ERROR("svc_alarm: failed to create poll thread");
        s_thread_running = false;
        return SW_ERR_HW;
    }

    LOG_INFO("svc_alarm init ok, %d alarms loaded", s_alarm_count);
    return SW_OK;
}

void svc_alarm_register_poll_fn(AlarmSignalPollFn fn)   { s_poll_fn      = fn; }
void svc_alarm_register_emc_reset_fn(AlarmEmcResetFn fn){ s_emc_reset_fn = fn; }
void svc_alarm_register_vfd_read_fn(AlarmVfdReadFn fn)  { s_vfd_read_fn  = fn; }

void svc_alarm_set_raw_trigger(uint16_t code, bool triggered, bool just_notice)
{
    int idx = find_entry(code);
    if (idx < 0) {
        return;
    }
    /* 在 poll_thread 内调用，不需要额外加锁（已在锁内）*/
    s_states[idx].raw_triggered = triggered;
    s_states[idx].just_notice   = just_notice;
}

void svc_alarm_set_state(uint16_t code, bool active, bool just_notice)
{
    int idx = find_entry(code);
    if (idx < 0) {
        return;
    }
    pthread_mutex_lock(&s_mutex);
    s_states[idx].is_active   = active;
    s_states[idx].just_notice = just_notice;
    if (active) {
        LOG_WARN("ALARM DIRECT SET code=%u [%s]",
                 (unsigned)code, g_alarm_table[idx].desc);
    }
    pthread_mutex_unlock(&s_mutex);
}

bool svc_alarm_is_active(uint16_t code)
{
    int  idx;
    bool ret = false;

    pthread_mutex_lock(&s_mutex);
    idx = find_entry(code);
    if (idx >= 0) {
        ret = s_states[idx].is_active;
    }
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

bool svc_alarm_has_error(void)
{
    int  i;
    bool has = false;

    pthread_mutex_lock(&s_mutex);
    for (i = 0; i < s_alarm_count; i++) {
        if (s_states[i].is_active &&
            !s_states[i].just_notice &&
            g_alarm_table[i].level == ALARM_LEVEL_ERROR) {
            has = true;
            break;
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return has;
}

void svc_alarm_manual_reset(void)
{
    int i;

    pthread_mutex_lock(&s_mutex);
    for (i = 0; i < s_alarm_count; i++) {
        if (s_states[i].is_active &&
            (g_alarm_table[i].recover & ALARM_RECOVER_MANUAL)) {
            s_states[i].is_active      = false;
            s_states[i].raw_triggered  = false;
            s_states[i].trigger_cnt_ms = 0;
            s_states[i].recover_cnt_ms = 0;
            LOG_INFO("ALARM MANUAL RESET code=%u", (unsigned)g_alarm_table[i].code);
        }
    }
    pthread_mutex_unlock(&s_mutex);
}

SvcAlarmFlags_t *svc_alarm_flags(void)
{
    return &s_flags;
}

/* -------------------------------------------------------------------------
 * CLI 调试接口（alarm 命令域）
 * ------------------------------------------------------------------------- */
int alarm_debug_ctl(char *cmd, char *p1, char *p2)
{
    int i;
    (void)p1;
    (void)p2;
    if (cmd == NULL) { return 0; }

    if (strcmp(cmd, "status") == 0) {
        bool any = false;
        pthread_mutex_lock(&s_mutex);
        for (i = 0; i < s_alarm_count; i++) {
            if (s_states[i].is_active) {
                LOG_INFO("alarm: code=%u level=%d desc=%s",
                         (unsigned)g_alarm_table[i].code,
                         (int)g_alarm_table[i].level,
                         g_alarm_table[i].desc);
                any = true;
            }
        }
        pthread_mutex_unlock(&s_mutex);
        if (!any) {
            LOG_INFO("alarm: no active alarms");
        }
        return 1;
    }

    if (strcmp(cmd, "reset") == 0) {
        svc_alarm_manual_reset();
        LOG_INFO("alarm: manual reset done");
        return 1;
    }

    return 0;
}
