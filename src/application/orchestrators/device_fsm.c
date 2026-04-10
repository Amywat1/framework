/**
 * @file    device_fsm.c
 * @brief   设备顶层有限状态机实现（事件驱动）
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "application/orchestrators/device_fsm.h"
#include "application/orchestrators/wash_orchestrator.h"
#include "service/dev_ctx/dev_ctx.h"
#include "service/svc_param/svc_param.h"
#include "domain/device/gate.h"
#include "domain/device/gantry.h"
#include "domain/safety/alarm_core.h"
#include "domain/safety/safety_fsm.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"
#include <pthread.h>

static dev_state_t     s_state = DEV_STATE_INIT;
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

/* -------------------------------------------------------------------------
 * 内部辅助
 * ------------------------------------------------------------------------- */
static dev_state_t get_state(void)
{
    dev_state_t st;
    pthread_mutex_lock(&s_mutex);
    st = s_state;
    pthread_mutex_unlock(&s_mutex);
    return st;
}

static void set_state(dev_state_t new_state)
{
    pthread_mutex_lock(&s_mutex);
    s_state = new_state;
    pthread_mutex_unlock(&s_mutex);
    dev_ctx_set_device_state(new_state);
    LOG_INFO("device_fsm: → %d", (int)new_state);
}

/* -------------------------------------------------------------------------
 * 事件处理函数（在 event_dispatch_thread 上下文执行）
 * ------------------------------------------------------------------------- */

/* EVT_CMD_ORDER (param = wash_mode_t) */
static void on_cmd_order(const event_t *evt)
{
    if (get_state() != DEV_STATE_IDLE)
    {
        LOG_WARN("device_fsm: ORDER ignored (not IDLE)");
        return;
    }

    wash_mode_t mode = (wash_mode_t)evt->param;
    if (mode >= WASH_MODE_MAX)
    {
        /* param 无效时从参数表读取 */
        mode = (wash_mode_t)svc_param_get_int(PARAM_KEY_WASH_MODE,
                                               (int)WASH_MODE_STANDARD);
    }

    if (wash_orchestrator_start(mode) != SW_OK)
    {
        LOG_ERROR("device_fsm: wash_orchestrator_start failed");
        return;
    }

    set_state(DEV_STATE_RUN);
    (void)gate_block();
    (void)gate_set_light(HAL_LIGHT_RED);
    LOG_INFO("device_fsm: IDLE → RUN mode=%d", (int)mode);
}

/* EVT_CMD_STOP_OPERATION */
static void on_cmd_stop_op(const event_t *evt)
{
    (void)evt;
    if (get_state() != DEV_STATE_IDLE)
    {
        LOG_WARN("device_fsm: STOP_OPERATION ignored (not IDLE)");
        return;
    }
    set_state(DEV_STATE_STOP);
    (void)gate_set_light(HAL_LIGHT_OFF);
    LOG_INFO("device_fsm: IDLE → STOP");
}

/* EVT_CMD_RESUME_OPERATION */
static void on_cmd_resume_op(const event_t *evt)
{
    (void)evt;
    if (get_state() != DEV_STATE_STOP)
    {
        LOG_WARN("device_fsm: RESUME_OPERATION ignored (not STOP)");
        return;
    }
    set_state(DEV_STATE_IDLE);
    (void)gate_allow();
    LOG_INFO("device_fsm: STOP → IDLE");
}

/* EVT_CMD_RESET_FAULT */
static void on_cmd_reset_fault(const event_t *evt)
{
    (void)evt;
    if (get_state() != DEV_STATE_FAULT)
    {
        LOG_WARN("device_fsm: RESET_FAULT ignored (not FAULT)");
        return;
    }

    alarm_core_manual_reset();
    safety_fsm_reevaluate();

    if (!alarm_core_has_error())
    {
        set_state(DEV_STATE_IDLE);
        (void)gate_allow();
        LOG_INFO("device_fsm: FAULT → IDLE (reset ok)");
    }
    else
    {
        LOG_WARN("device_fsm: RESET_FAULT: alarm still active");
    }
}

/* EVT_CMD_HOME_DEVICE */
static void on_cmd_home(const event_t *evt)
{
    (void)evt;
    if (get_state() != DEV_STATE_IDLE)
    {
        LOG_WARN("device_fsm: HOME_DEVICE ignored (not IDLE)");
        return;
    }
    /* 龙门归位（异步，EVT_COMP_HOME_DONE 时结束，状态保持 IDLE）*/
    (void)gantry_home_start(4000U); /* 4000 = 40.00Hz 慢速 */
    LOG_INFO("device_fsm: homing started");
}

/* EVT_WASH_DONE */
static void on_wash_done(const event_t *evt)
{
    (void)evt;
    if (get_state() != DEV_STATE_RUN)
    {
        return;
    }

    /* COMPLETE 动作（内联，状态不对外暴露）*/
    (void)gate_allow();
    (void)gate_set_light(HAL_LIGHT_GREEN);

    set_state(DEV_STATE_IDLE);
    LOG_INFO("device_fsm: RUN → IDLE (wash done)");
}

/* EVT_WASH_ABORTED */
static void on_wash_aborted(const event_t *evt)
{
    (void)evt;
    if (get_state() != DEV_STATE_RUN)
    {
        return;
    }
    set_state(DEV_STATE_FAULT);
    (void)gate_block();
    (void)gate_set_light(HAL_LIGHT_RED);
    LOG_WARN("device_fsm: RUN → FAULT (wash aborted, reason=%u)", (unsigned)evt->param);
}

/* EVT_SAFETY_LOCKOUT：紧急停机 */
static void on_safety_lockout(const event_t *evt)
{
    (void)evt;
    if (get_state() == DEV_STATE_RUN)
    {
        wash_orchestrator_abort();
        set_state(DEV_STATE_FAULT);
        (void)gate_block();
        (void)gate_set_light(HAL_LIGHT_RED);
        LOG_WARN("device_fsm: RUN → FAULT (LOCKOUT)");
    }
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t device_fsm_init(void)
{
    sw_err_t ret;

    /* 所有组件已由 bootstrap 初始化完毕，直接进入 IDLE */
    set_state(DEV_STATE_IDLE);
    (void)gate_allow();
    (void)gate_set_light(HAL_LIGHT_GREEN);

    ret = event_subscribe(EVT_CMD_ORDER,              on_cmd_order);
    if (ret != SW_OK) { return ret; }
    ret = event_subscribe(EVT_CMD_STOP_OPERATION,     on_cmd_stop_op);
    if (ret != SW_OK) { return ret; }
    ret = event_subscribe(EVT_CMD_RESUME_OPERATION,   on_cmd_resume_op);
    if (ret != SW_OK) { return ret; }
    ret = event_subscribe(EVT_CMD_RESET_FAULT,        on_cmd_reset_fault);
    if (ret != SW_OK) { return ret; }
    ret = event_subscribe(EVT_CMD_HOME_DEVICE,        on_cmd_home);
    if (ret != SW_OK) { return ret; }
    ret = event_subscribe(EVT_WASH_DONE,              on_wash_done);
    if (ret != SW_OK) { return ret; }
    ret = event_subscribe(EVT_WASH_ABORTED,           on_wash_aborted);
    if (ret != SW_OK) { return ret; }
    ret = event_subscribe(EVT_SAFETY_LOCKOUT,         on_safety_lockout);
    if (ret != SW_OK) { return ret; }

    LOG_INFO("device_fsm: init ok (IDLE)");
    return SW_OK;
}

dev_state_t device_fsm_get_state(void)
{
    return get_state();
}
