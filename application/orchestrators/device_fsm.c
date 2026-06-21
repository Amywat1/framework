/**
 * @file    device_fsm.c
 * @brief   设备顶层有限状态机实现（事件驱动）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "application/orchestrators/device_fsm.h"
#include "application/orchestrators/wash_orchestrator.h"
#include "service/dev_ctx/dev_ctx.h"
#include "service/svc_param/svc_param.h"
#include "domain/device/gate.h"
#include "domain/device/unit/gantry.h"
#include "domain/safety/alarm_core.h"
#include "domain/safety/safety_fsm.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"
#include <stdbool.h>

/* true = 用户主动停止，EVT_WASH_ABORTED 时转 IDLE 而非 FAULT */
static bool s_manual_stop = false;

static dev_state_t get_state(void)
{
    return dev_ctx_get_device_state();
}

static void set_state(dev_state_t new_state)
{
    dev_ctx_set_device_state(new_state);
    LOG_INFO("device_fsm: → %d", (int)new_state);
}

static void apply_idle_indicator(void)
{
    (void)gate_allow();
    (void)gate_set_light(GATE_LIGHT_GREEN_BLINK);
}

static void apply_run_indicator(void)
{
    (void)gate_block();
    (void)gate_set_light(GATE_LIGHT_YELLOW_BLINK);
}

static void apply_fault_indicator(void)
{
    (void)gate_block();
    (void)gate_set_light(GATE_LIGHT_RED_BLINK);
}

static void apply_stop_indicator(void)
{
    (void)gate_set_light(GATE_LIGHT_OFF);
}

static void clear_manual_stop_flag(void)
{
    s_manual_stop = false;
}

/**
 * @brief  过滤 command_guard 通过后、入队期间状态变化导致的过期命令
 */
static bool cmd_stale_for_state(dev_state_t expected)
{
    return get_state() != expected;
}

static bool cmd_stale_for_start_wash(void)
{
    device_context_t ctx = dev_ctx_snapshot();

    return (ctx.safety_state != SAFETY_STATE_OK) ||
           (ctx.device_state != DEV_STATE_IDLE);
}

/* -------------------------------------------------------------------------
 * 事件处理函数（在 event_dispatch_thread 上下文执行）
 * ------------------------------------------------------------------------- */

/* EVT_CMD_ORDER (param = wash_mode_t) */
static void on_cmd_order(const event_t *evt)
{
    if (cmd_stale_for_start_wash())
    {
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

    clear_manual_stop_flag();
    set_state(DEV_STATE_RUN);
    apply_run_indicator();
    LOG_INFO("device_fsm: IDLE → RUN mode=%d", (int)mode);
}

/* EVT_CMD_STOP_OPERATION */
static void on_cmd_stop_op(const event_t *evt)
{
    (void)evt;
    if (cmd_stale_for_state(DEV_STATE_IDLE))
    {
        return;
    }
    set_state(DEV_STATE_STOP);
    apply_stop_indicator();
    LOG_INFO("device_fsm: IDLE → STOP");
}

/* EVT_CMD_RESUME_OPERATION */
static void on_cmd_resume_op(const event_t *evt)
{
    (void)evt;
    if (cmd_stale_for_state(DEV_STATE_STOP))
    {
        return;
    }
    set_state(DEV_STATE_IDLE);
    apply_idle_indicator();
    LOG_INFO("device_fsm: STOP → IDLE");
}

/* EVT_CMD_RESET_FAULT */
static void on_cmd_reset_fault(const event_t *evt)
{
    (void)evt;
    if (cmd_stale_for_state(DEV_STATE_FAULT))
    {
        return;
    }

    alarm_core_manual_reset();
    safety_fsm_reevaluate();

    if (!alarm_core_has_error())
    {
        set_state(DEV_STATE_IDLE);
        apply_idle_indicator();
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
    if (cmd_stale_for_state(DEV_STATE_IDLE))
    {
        return;
    }
    /* 龙门归位（异步，EVT_COMP_HOME_DONE 时结束，状态保持 IDLE）*/
    if (gantry_home_start(4000U) != SW_OK) /* 4000 = 40.00Hz 慢速 */
    {
        LOG_WARN("device_fsm: homing start failed");
        return;
    }
    set_state(DEV_STATE_COMPLETE);
    (void)gate_block();
    (void)gate_set_light(GATE_LIGHT_YELLOW_BLINK);
    LOG_INFO("device_fsm: IDLE → COMPLETE (homing started)");
}

/* EVT_COMP_HOME_DONE */
static void on_home_done(const event_t *evt)
{
    if (get_state() != DEV_STATE_COMPLETE)
    {
        return;
    }

    if ((sw_err_t)evt->param == SW_OK)
    {
        set_state(DEV_STATE_IDLE);
        apply_idle_indicator();
        LOG_INFO("device_fsm: COMPLETE → IDLE (homing done)");
    }
    else
    {
        clear_manual_stop_flag();
        set_state(DEV_STATE_FAULT);
        apply_fault_indicator();
        LOG_WARN("device_fsm: COMPLETE → FAULT (homing failed ret=%d)",
                 (int)((sw_err_t)evt->param));
    }
}

/* EVT_WASH_DONE */
static void on_wash_done(const event_t *evt)
{
    (void)evt;
    if (get_state() != DEV_STATE_RUN)
    {
        return;
    }

    set_state(DEV_STATE_IDLE);
    apply_idle_indicator();
    LOG_INFO("device_fsm: RUN → IDLE (wash done)");
}

/* EVT_CMD_STOP_WASH：用户主动停止当前洗车 */
static void on_cmd_stop_wash(const event_t *evt)
{
    (void)evt;
    if (cmd_stale_for_state(DEV_STATE_RUN))
    {
        return;
    }
    s_manual_stop = true; /* 标记为主动停止，EVT_WASH_ABORTED 时转 IDLE */
    wash_orchestrator_abort();
    LOG_INFO("device_fsm: manual stop requested");
}

/* EVT_WASH_ABORTED */
static void on_wash_aborted(const event_t *evt)
{
    if (get_state() != DEV_STATE_RUN)
    {
        return;
    }

    if (s_manual_stop)
    {
        /* 用户主动停止：恢复 IDLE，开放入口 */
        clear_manual_stop_flag();
        set_state(DEV_STATE_IDLE);
        apply_idle_indicator();
        LOG_INFO("device_fsm: RUN → IDLE (manual stop)");
    }
    else
    {
        /* 故障导致中止：进入 FAULT，等待复位 */
        clear_manual_stop_flag();
        set_state(DEV_STATE_FAULT);
        apply_fault_indicator();
        LOG_WARN("device_fsm: RUN → FAULT (wash aborted reason=%u)",
                 (unsigned)evt->param);
    }
}

/* EVT_SAFETY_LOCKOUT：设备态转 FAULT（运动中止由 emergency_handler 负责） */
static void on_safety_lockout(const event_t *evt)
{
    (void)evt;
    dev_state_t state = get_state();

    if ((state == DEV_STATE_RUN) || (state == DEV_STATE_COMPLETE))
    {
        clear_manual_stop_flag();
        set_state(DEV_STATE_FAULT);
        apply_fault_indicator();
        LOG_WARN("device_fsm: %d → FAULT (LOCKOUT)", (int)state);
    }
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t device_fsm_init(void)
{
    static const event_subscription_t s_subs[] = {
        { EVT_CMD_ORDER,            on_cmd_order       },
        { EVT_CMD_STOP_WASH,        on_cmd_stop_wash   },
        { EVT_CMD_STOP_OPERATION,   on_cmd_stop_op     },
        { EVT_CMD_RESUME_OPERATION, on_cmd_resume_op   },
        { EVT_CMD_RESET_FAULT,      on_cmd_reset_fault },
        { EVT_CMD_HOME_DEVICE,      on_cmd_home        },
        { EVT_COMP_HOME_DONE,       on_home_done       },
        { EVT_WASH_DONE,            on_wash_done       },
        { EVT_WASH_ABORTED,         on_wash_aborted    },
        { EVT_SAFETY_LOCKOUT,       on_safety_lockout  },
    };
    sw_err_t ret;

    /* 所有组件已由 bootstrap 初始化完毕，直接进入 IDLE */
    set_state(DEV_STATE_IDLE);
    apply_idle_indicator();

    ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK)
    {
        return ret;
    }

    LOG_INFO("device_fsm: init ok (IDLE)");
    return SW_OK;
}
