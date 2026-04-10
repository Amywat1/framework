/**
 * @file    safety_fsm.c
 * @brief   安全状态机实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "domain/safety/safety_fsm.h"
#include "domain/safety/alarm_core.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"
#include <pthread.h>

/* -------------------------------------------------------------------------
 * 内部状态
 * ------------------------------------------------------------------------- */
static safety_state_t  s_state  = SAFETY_STATE_OK;
static pthread_mutex_t s_mutex  = PTHREAD_MUTEX_INITIALIZER;

/* -------------------------------------------------------------------------
 * 内部：根据当前激活报警重新计算状态并发布事件（调用时不持锁）
 * ------------------------------------------------------------------------- */
static void do_reevaluate(void)
{
    safety_state_t new_state;
    safety_state_t old_state;

    if (alarm_core_has_error())
    {
        new_state = SAFETY_STATE_LOCKOUT;
    }
    else if (alarm_core_has_warning())
    {
        new_state = SAFETY_STATE_WARNING;
    }
    else
    {
        new_state = SAFETY_STATE_OK;
    }

    pthread_mutex_lock(&s_mutex);
    old_state = s_state;
    s_state   = new_state;
    pthread_mutex_unlock(&s_mutex);

    if (new_state == old_state)
    {
        return;
    }

    /* 发布安全状态变化事件 */
    if (new_state == SAFETY_STATE_LOCKOUT)
    {
        (void)event_publish(EVT_SAFETY_LOCKOUT, 0U);
        LOG_ERROR("safety_fsm: → LOCKOUT");
    }
    else if (new_state == SAFETY_STATE_WARNING)
    {
        (void)event_publish(EVT_SAFETY_WARNING, 0U);
        LOG_WARN("safety_fsm: → WARNING");
    }
    else
    {
        (void)event_publish(EVT_SAFETY_CLEARED, 0U);
        LOG_INFO("safety_fsm: → OK (CLEARED)");
    }
}

/* -------------------------------------------------------------------------
 * 事件处理函数（在 event_dispatch_thread 上下文调用）
 * ------------------------------------------------------------------------- */
static void on_alarm_triggered(const event_t *evt)
{
    do_reevaluate();
    (void)evt; /* code 在 do_reevaluate 内部通过 alarm_core 查询，此处不需要 */
}

static void on_alarm_cleared(const event_t *evt)
{
    do_reevaluate();
    (void)evt;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t safety_fsm_init(void)
{
    sw_err_t ret;

    pthread_mutex_lock(&s_mutex);
    s_state = SAFETY_STATE_OK;
    pthread_mutex_unlock(&s_mutex);

    ret = event_subscribe(EVT_ALARM_TRIGGERED, on_alarm_triggered);
    if (ret != SW_OK)
    {
        LOG_ERROR("safety_fsm_init: subscribe EVT_ALARM_TRIGGERED failed");
        return ret;
    }

    ret = event_subscribe(EVT_ALARM_CLEARED, on_alarm_cleared);
    if (ret != SW_OK)
    {
        LOG_ERROR("safety_fsm_init: subscribe EVT_ALARM_CLEARED failed");
        return ret;
    }

    LOG_INFO("safety_fsm: init ok");
    return SW_OK;
}

safety_state_t safety_fsm_get_state(void)
{
    safety_state_t state;
    pthread_mutex_lock(&s_mutex);
    state = s_state;
    pthread_mutex_unlock(&s_mutex);
    return state;
}

void safety_fsm_reevaluate(void)
{
    do_reevaluate();
}
