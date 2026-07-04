/**
 * @file    device_fsm.c
 * @brief   设备顶层有限状态机实现（事件驱动）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    状态转移一览：
 *
 *   IDLE ──EVT_CMD_ORDER──────────────────────────────────────► RUN
 *   IDLE ──EVT_CMD_STOP_OPERATION────────────────────────────► STOP
 *   IDLE ──EVT_HW_ESTOP_ON / EVT_SAFETY_LOCKOUT──────────────► SUSPENDING
 *
 *   RUN  ──EVT_WASH_DONE─────────────────────────────────────► IDLE
 *   RUN  ──EVT_WASH_ABORTED (手动停止)────────────────────────► IDLE
 *   RUN  ──EVT_WASH_ABORTED (故障，LOCKOUT 已先切换到 SUSPENDING)
 *          → 此时 state 已为 SUSPENDING，on_wash_aborted 直接忽略
 *   RUN  ──EVT_HW_ESTOP_ON / EVT_SAFETY_LOCKOUT──────────────► SUSPENDING
 *
 *   SUSPENDING ──EVT_SAFETY_HOME_DONE────────────────────────► FAULT
 *   SUSPENDING ──EVT_HW_ESTOP_ON────────────────────────────── 忽略（已在停机态）
 *
 *   FAULT ──EVT_CMD_HOME_DEVICE──────────────────────────────► HOMING
 *   FAULT ──EVT_CMD_RESET_FAULT──────────────────────────────► IDLE
 *   FAULT ──EVT_HW_ESTOP_ON────────────────────────────────── 忽略（已在停机态）
 *
 *   HOMING ──EVT_COMP_HOME_DONE (OK)──────────────────────────► IDLE
 *   HOMING ──EVT_COMP_HOME_DONE (fail)────────────────────────► FAULT
 *   HOMING ──EVT_HW_ESTOP_ON / EVT_SAFETY_LOCKOUT────────────► SUSPENDING
 *
 *   STOP ──EVT_CMD_RESUME_OPERATION──────────────────────────► IDLE
 *   STOP ──EVT_HW_ESTOP_ON / EVT_SAFETY_LOCKOUT──────────────► SUSPENDING
 *
 *   安全归位（SUSPENDING 阶段）由 emergency_handler 负责执行并发布
 *   EVT_SAFETY_HOME_DONE；device_fsm 不直接操作硬件。
 *
 *   停车检测（IDLE 收到 EVT_CMD_ORDER 时的前置条件）待 m8_signal_table
 *   增加 FRONT_WHEEL / REAR_LOCK 信号后在此接入。
 */

#include "framework/application/orchestrators/device_fsm.h"
#include "framework/application/orchestrators/wash_orchestrator.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/services/param/svc_param.h"
#include "framework/domain/device_control/mechanism/gantry.h"
#include "framework/domain/safety/alarm/alarm_core.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 内部辅助
 * ------------------------------------------------------------------------- */

static const char *state_name(dev_state_t s)
{
    static const char *const k_names[] = {
        "INIT", "IDLE", "RUN", "SUSPENDING", "FAULT", "HOMING", "STOP"
    };
    if ((unsigned)s < sizeof(k_names) / sizeof(k_names[0]))
    {
        return k_names[(unsigned)s];
    }
    return "?";
}

/* 用户主动停止标志：true 时 EVT_WASH_ABORTED 转 IDLE，否则忽略（LOCKOUT 已先转 SUSPENDING）*/
static bool s_manual_stop = false;

static dev_state_t get_state(void)
{
    return dev_ctx_get_device_state();
}

static void set_state(dev_state_t next)
{
    LOG_INFO("device_fsm: %s → %s", state_name(get_state()), state_name(next));
    dev_ctx_set_device_state(next);
}

/* -------------------------------------------------------------------------
 * 事件处理函数（在 event_dispatch_thread 上下文执行，无需加锁）
 * ------------------------------------------------------------------------- */

/* EVT_HW_ESTOP_ON：物理急停按下（高优先级队列，最先处理）
 *   任意活跃态 → SUSPENDING；FAULT / SUSPENDING 已是停机态则忽略 */
static void on_estop_on(const event_t *evt)
{
    (void)evt;
    dev_state_t s = get_state();
    if (s == DEV_STATE_FAULT || s == DEV_STATE_SUSPENDING)
    {
        return;
    }
    s_manual_stop = false;
    set_state(DEV_STATE_SUSPENDING);
}

/* EVT_SAFETY_LOCKOUT：报警聚合到 LOCKOUT 等级
 *   任意活跃态 → SUSPENDING；FAULT / SUSPENDING 已是停机态则忽略 */
static void on_safety_lockout(const event_t *evt)
{
    (void)evt;
    dev_state_t s = get_state();
    if (s == DEV_STATE_FAULT || s == DEV_STATE_SUSPENDING)
    {
        return;
    }
    s_manual_stop = false;
    set_state(DEV_STATE_SUSPENDING);
}

/* EVT_SAFETY_HOME_DONE：emergency_handler 完成安全归位
 *   SUSPENDING → FAULT */
static void on_safety_home_done(const event_t *evt)
{
    (void)evt;
    if (get_state() != DEV_STATE_SUSPENDING)
    {
        return;
    }
    set_state(DEV_STATE_FAULT);
}

/* EVT_CMD_ORDER：启动洗车（param = wash_mode_t）
 *   IDLE → RUN */
static void on_cmd_order(const event_t *evt)
{
    if (get_state() != DEV_STATE_IDLE)
    {
        return;
    }

    wash_mode_t mode = (wash_mode_t)evt->param;
    if (mode >= WASH_MODE_MAX)
    {
        mode = (wash_mode_t)svc_param_get_int(PARAM_KEY_WASH_MODE,
                                               (int)WASH_MODE_STANDARD);
    }

    /* TODO: 停车传感器前置检测（待 m8_signal_table 增加 FRONT_WHEEL / REAR_LOCK
     *       信号后接入 m8_signal_is_active()，不满足时拒绝并提示）*/

    if (wash_orchestrator_start(mode) != SW_OK)
    {
        LOG_ERROR("device_fsm: wash_orchestrator_start failed");
        return;
    }

    s_manual_stop = false;
    set_state(DEV_STATE_RUNNING);
}

/* EVT_CMD_STOP_WASH：用户主动停止当前洗车
 *   RUN → （异步等待 EVT_WASH_ABORTED） */
static void on_cmd_stop_wash(const event_t *evt)
{
    (void)evt;
    if (get_state() != DEV_STATE_RUNNING)
    {
        return;
    }
    s_manual_stop = true;
    wash_orchestrator_abort();
    LOG_INFO("device_fsm: manual stop requested, waiting for WASH_ABORTED");
}

/* EVT_WASH_DONE：洗车流程正常完成
 *   RUN → IDLE */
static void on_wash_done(const event_t *evt)
{
    (void)evt;
    if (get_state() != DEV_STATE_RUNNING)
    {
        return;
    }
    s_manual_stop = false;
    set_state(DEV_STATE_IDLE);
}

/* EVT_WASH_ABORTED：洗车中止
 *   - 手动停止（s_manual_stop）：RUN → IDLE
 *   - 故障触发（LOCKOUT/ESTOP 已先执行 on_safety_lockout/on_estop_on）：
 *     state 已切至 SUSPENDING，此处直接忽略 */
static void on_wash_aborted(const event_t *evt)
{
    (void)evt;
    if (get_state() != DEV_STATE_RUNNING)
    {
        return;
    }

    if (s_manual_stop)
    {
        s_manual_stop = false;
        set_state(DEV_STATE_IDLE);
    }
    else
    {
        /* 无 LOCKOUT 触发的内部故障中止（如步骤超时）：
         * 进入 SUSPENDING，等待 emergency_handler 执行安全归位后发布 SAFETY_HOME_DONE。
         * 前提：wash_orchestrator 内部故障须通过 alarm_core_trigger 触发 LOCKOUT，
         * 确保 emergency_handler 已介入；否则 SUSPENDING 需由上层监控超时处理。*/
        s_manual_stop = false;
        set_state(DEV_STATE_SUSPENDING);
        LOG_WARN("device_fsm: wash aborted without explicit LOCKOUT, entering SUSPENDING");
    }
}

/* EVT_CMD_HOME_DEVICE：人工触发完整归位
 *   FAULT → HOMING */
static void on_cmd_home(const event_t *evt)
{
    (void)evt;
    if (get_state() != DEV_STATE_FAULT)
    {
        return;
    }

    if (gantry_home() != SW_OK)
    {
        LOG_WARN("device_fsm: gantry_home failed");
        return;
    }
    set_state(DEV_STATE_HOMING);
}

/* EVT_COMP_HOME_DONE：归位完成（成功/失败由 param 区分）
 *   HOMING → IDLE (OK) / FAULT (fail) */
static void on_home_done(const event_t *evt)
{
    if (get_state() != DEV_STATE_HOMING)
    {
        return;
    }

    if ((sw_err_t)evt->param == SW_OK)
    {
        set_state(DEV_STATE_IDLE);
    }
    else
    {
        set_state(DEV_STATE_FAULT);
        LOG_WARN("device_fsm: homing failed ret=%d", (int)(sw_err_t)evt->param);
    }
}

/* EVT_CMD_RESET_FAULT：人工复位报警并解除设备停机态
 *   - 强制清除所有活跃报警（含 LATCHED 锁存报警）
 *   - FAULT → IDLE（从 WARNING+IDLE 触发时设备状态不变）*/
static void on_cmd_reset_fault(const event_t *evt)
{
    (void)evt;
    /* 无论设备处于 FAULT 还是 WARNING+IDLE，都先强制清除所有锁存报警 */
    (void)alarm_core_reset_alarms();
    if (get_state() == DEV_STATE_FAULT)
    {
        set_state(DEV_STATE_IDLE);
    }
}

/* EVT_CMD_STOP_OPERATION：运营暂停（仅允许从 IDLE 切换）
 *   IDLE → STOP */
static void on_cmd_stop_op(const event_t *evt)
{
    (void)evt;
    if (get_state() != DEV_STATE_IDLE)
    {
        return;
    }
    set_state(DEV_STATE_STOP);
}

/* EVT_CMD_RESUME_OPERATION：恢复运营
 *   STOP → IDLE */
static void on_cmd_resume_op(const event_t *evt)
{
    (void)evt;
    if (get_state() != DEV_STATE_STOP)
    {
        return;
    }
    set_state(DEV_STATE_IDLE);
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */

sw_err_t device_fsm_init(void)
{
    static const event_subscription_t s_subs[] = {
        /* 高优先级硬件事件 */
        { EVT_HW_ESTOP_ON,          on_estop_on         },
        /* 安全域事件 */
        { EVT_SAFETY_LOCKOUT,       on_safety_lockout   },
        { EVT_SAFETY_HOME_DONE,     on_safety_home_done },
        /* 命令事件 */
        { EVT_CMD_ORDER,            on_cmd_order        },
        { EVT_CMD_STOP_WASH,        on_cmd_stop_wash    },
        { EVT_CMD_STOP_OPERATION,   on_cmd_stop_op      },
        { EVT_CMD_RESUME_OPERATION, on_cmd_resume_op    },
        { EVT_CMD_RESET_FAULT,      on_cmd_reset_fault  },
        { EVT_CMD_HOME_DEVICE,      on_cmd_home         },
        /* 流程完成事件 */
        { EVT_COMP_HOME_DONE,       on_home_done        },
        { EVT_WASH_DONE,            on_wash_done        },
        { EVT_WASH_ABORTED,         on_wash_aborted     },
    };

    set_state(DEV_STATE_IDLE);

    sw_err_t ret = event_subscribe_table(s_subs,
                                         sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK)
    {
        return ret;
    }

    LOG_INFO("device_fsm: init ok (IDLE)");
    return SW_OK;
}
