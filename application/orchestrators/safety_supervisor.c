/**
 * @file    safety_supervisor.c
 * @brief   安全监督者实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "application/orchestrators/safety_supervisor.h"
#include "service/dev_ctx/dev_ctx.h"
#include "domain/safety/alarm_core.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"

/* -------------------------------------------------------------------------
 * 事件处理函数（在 event_dispatch_thread 上下文执行）
 * ------------------------------------------------------------------------- */
static void on_safety_lockout(const event_t *evt)
{
    (void)evt;
    dev_ctx_set_safety_state(SAFETY_STATE_LOCKOUT);
    dev_ctx_set_alarm_state(true);
    LOG_WARN("safety_supervisor: LOCKOUT → dev_ctx updated");
}

static void on_safety_warning(const event_t *evt)
{
    (void)evt;
    dev_ctx_set_safety_state(SAFETY_STATE_WARNING);
    LOG_WARN("safety_supervisor: WARNING → dev_ctx updated");
}

static void on_safety_cleared(const event_t *evt)
{
    (void)evt;
    dev_ctx_set_safety_state(SAFETY_STATE_OK);
    dev_ctx_set_alarm_state(false);
    LOG_INFO("safety_supervisor: CLEARED → dev_ctx updated");
}

static void on_alarm_triggered(const event_t *evt)
{
    (void)evt;
    /* 只要有 ERROR 级报警激活，就更新 has_error 标志 */
    dev_ctx_set_alarm_state(alarm_core_has_error());
}

static void on_alarm_cleared(const event_t *evt)
{
    (void)evt;
    dev_ctx_set_alarm_state(alarm_core_has_error());
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t safety_supervisor_init(void)
{
    sw_err_t ret;

    ret = event_subscribe(EVT_SAFETY_LOCKOUT,    on_safety_lockout);
    if (ret != SW_OK) { return ret; }

    ret = event_subscribe(EVT_SAFETY_WARNING,    on_safety_warning);
    if (ret != SW_OK) { return ret; }

    ret = event_subscribe(EVT_SAFETY_CLEARED,    on_safety_cleared);
    if (ret != SW_OK) { return ret; }

    ret = event_subscribe(EVT_ALARM_TRIGGERED,   on_alarm_triggered);
    if (ret != SW_OK) { return ret; }

    ret = event_subscribe(EVT_ALARM_CLEARED,     on_alarm_cleared);
    if (ret != SW_OK) { return ret; }

    LOG_INFO("safety_supervisor: init ok");
    return SW_OK;
}
