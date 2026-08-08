/**
 * @file    operational_mode.c
 * @brief   OperationalMode 聚合根实现（10 态状态机，表驱动）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "domain/op_mode/operational_mode.h"

#include "common/event_types.h"
#include "common/log.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/ports/outbound/machine/machine_ops_port.h"
#include "runtime/event_bus/event_bus.h"

typedef enum {
    OP_PERM_DENIED = 0,
    OP_PERM_ALLOWED,
    OP_PERM_CONDITIONAL, /**< 允许，但需通过运行时条件检查（急停、服务开关等）*/
} op_perm_t;

static operational_mode_t s_mode            = OP_MODE_INIT;
static bool               s_estop_active    = false;
static bool               s_service_enabled = true;

static void op_mode_set_service_enabled(bool enabled);

static const char *op_mode_name(operational_mode_t mode)
{
    switch (mode) {
    case OP_MODE_INIT:
        return "INIT";
    case OP_MODE_STOPPED:
        return "STOPPED";
    case OP_MODE_HOMING:
        return "HOMING";
    case OP_MODE_IDLE:
        return "IDLE";
    case OP_MODE_WASHING:
        return "WASHING";
    case OP_MODE_ABORT_HOMING:
        return "ABORT_HOMING";
    case OP_MODE_WASH_DONE:
        return "WASH_DONE";
    case OP_MODE_SELF_CHECK:
        return "SELF_CHECK";
    case OP_MODE_EXCEPTION:
        return "EXCEPTION";
    case OP_MODE_RECOVERING:
        return "RECOVERING";
    default:
        return "UNKNOWN";
    }
}

static const char *wash_abort_name(wash_abort_cause_t cause)
{
    switch (cause) {
    case WASH_ABORT_MANUAL:
        return "manual";
    case WASH_ABORT_CRITICAL:
        return "critical";
    case WASH_ABORT_STEP_TIMEOUT:
        return "timeout";
    case WASH_ABORT_INTERNAL:
        return "internal";
    case WASH_ABORT_ESTOP:
        return "estop";
    default:
        return "unknown";
    }
}

static void publish_mode_changed(operational_mode_t from, operational_mode_t to)
{
    (void)event_publish(EVT_OP_MODE_CHANGED, op_mode_changed_evt_param(from, to));
}

static void publish_context_sync(void)
{
    (void)event_publish(EVT_OP_MODE_CONTEXT_SYNC, 0U);
}

static void set_mode(operational_mode_t next, const char *cause)
{
    operational_mode_t from;
    sw_log_level_t     level;

    if (s_mode == next) {
        return;
    }

    from   = s_mode;
    level  = ((next == OP_MODE_EXCEPTION) || (next == OP_MODE_ABORT_HOMING)) ? SW_LOG_WARN : SW_LOG_INFO;
    s_mode = next;

    if (cause != NULL) {
        sw_log_write(level, SW_LOG_COMPONENT, "op_mode: %s -> %s (%s)", op_mode_name(from), op_mode_name(next), cause);
    } else {
        sw_log_write(level, SW_LOG_COMPONENT, "op_mode: %s -> %s", op_mode_name(from), op_mode_name(next));
    }

    publish_mode_changed(from, next);
}

/*
 * 命令权限矩阵
 *
 * 列顺序与 operational_mode_t 枚举一致：
 *   INIT, STOPPED, HOMING, IDLE, WASHING, ABORT_HOMING, WASH_DONE, SELF_CHECK, EXCEPTION, RECOVERING
 *
 * CONDITIONAL：允许但需后续运行时检查（急停、服务开关等）
 */
static const op_perm_t k_cmd_matrix[DEV_CMD_MAX][OP_MODE_RECOVERING + 1] =
{
    /* DEV_CMD_START_WASH：仅 IDLE 允许，受 service_enabled + 无阻塞告警约束 */
    [DEV_CMD_START_WASH] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_DENIED,      /* STOPPED     */
        OP_PERM_DENIED,      /* HOMING      */
        OP_PERM_CONDITIONAL, /* IDLE        */
        OP_PERM_DENIED,      /* WASHING     */
        OP_PERM_DENIED,      /* ABORT_HOMING*/
        OP_PERM_DENIED,      /* WASH_DONE   */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_DENIED,      /* EXCEPTION   */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    /* DEV_CMD_STOP_WASH：仅 WASHING */
    [DEV_CMD_STOP_WASH] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_DENIED,      /* STOPPED     */
        OP_PERM_DENIED,      /* HOMING      */
        OP_PERM_DENIED,      /* IDLE        */
        OP_PERM_ALLOWED,     /* WASHING     */
        OP_PERM_DENIED,      /* ABORT_HOMING*/
        OP_PERM_DENIED,      /* WASH_DONE   */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_DENIED,      /* EXCEPTION   */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    /* DEV_CMD_STOP_OPERATION：停运 → STOPPED 且关闭运营总开关（IDLE / WASH_DONE）*/
    [DEV_CMD_STOP_OPERATION] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_DENIED,      /* STOPPED     */
        OP_PERM_DENIED,      /* HOMING      */
        OP_PERM_ALLOWED,     /* IDLE        */
        OP_PERM_DENIED,      /* WASHING     */
        OP_PERM_DENIED,      /* ABORT_HOMING*/
        OP_PERM_ALLOWED,     /* WASH_DONE   */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_DENIED,      /* EXCEPTION   */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    /* DEV_CMD_RESUME_OPERATION：仅 STOPPED 下重新授权运营（仍须 HOME 进 IDLE）*/
    [DEV_CMD_RESUME_OPERATION] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_CONDITIONAL, /* STOPPED     */
        OP_PERM_DENIED,      /* HOMING      */
        OP_PERM_DENIED,      /* IDLE        */
        OP_PERM_DENIED,      /* WASHING     */
        OP_PERM_DENIED,      /* ABORT_HOMING*/
        OP_PERM_DENIED,      /* WASH_DONE   */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_DENIED,      /* EXCEPTION   */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    /* DEV_CMD_MANUAL_ACTUATOR：STOPPED 直接允许；EXCEPTION 需无急停 */
    [DEV_CMD_MANUAL_ACTUATOR] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_ALLOWED,     /* STOPPED     */
        OP_PERM_DENIED,      /* HOMING      */
        OP_PERM_DENIED,      /* IDLE        */
        OP_PERM_DENIED,      /* WASHING     */
        OP_PERM_DENIED,      /* ABORT_HOMING*/
        OP_PERM_DENIED,      /* WASH_DONE   */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_CONDITIONAL, /* EXCEPTION   */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    /* DEV_CMD_START_SELF_CHECK：STOPPED / EXCEPTION 均允许 */
    [DEV_CMD_START_SELF_CHECK] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_ALLOWED,     /* STOPPED     */
        OP_PERM_DENIED,      /* HOMING      */
        OP_PERM_DENIED,      /* IDLE        */
        OP_PERM_DENIED,      /* WASHING     */
        OP_PERM_DENIED,      /* ABORT_HOMING*/
        OP_PERM_DENIED,      /* WASH_DONE   */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_ALLOWED,     /* EXCEPTION   */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    /* DEV_CMD_RECOVER：STOPPED 正常归位，EXCEPTION 故障恢复，IDLE 幂等成功 */
    [DEV_CMD_RECOVER] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_CONDITIONAL, /* STOPPED     */
        OP_PERM_DENIED,      /* HOMING      */
        OP_PERM_ALLOWED,     /* IDLE        */
        OP_PERM_DENIED,      /* WASHING     */
        OP_PERM_DENIED,      /* ABORT_HOMING*/
        OP_PERM_DENIED,      /* WASH_DONE   */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_CONDITIONAL, /* EXCEPTION   */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    /* DEV_CMD_STOP_ALL_OUTPUTS：紧急停止所有输出，多数状态均允许 */
    [DEV_CMD_STOP_ALL_OUTPUTS] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_ALLOWED,     /* STOPPED     */
        OP_PERM_DENIED,      /* HOMING      */
        OP_PERM_ALLOWED,     /* IDLE        */
        OP_PERM_ALLOWED,     /* WASHING     */
        OP_PERM_ALLOWED,     /* ABORT_HOMING*/
        OP_PERM_ALLOWED,     /* WASH_DONE   */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_ALLOWED,     /* EXCEPTION   */
        OP_PERM_ALLOWED,     /* RECOVERING  */
    },
};

static dev_cmd_decision_t make_denied(op_reject_reason_t reason)
{
    dev_cmd_decision_t d;

    d.verdict        = OP_CMD_DENIED;
    d.reason         = reason;
    d.pending_effect = DEV_CMD_EFFECT_NONE;
    return d;
}

static const dev_cmd_effect_t k_cmd_effects[DEV_CMD_MAX] = {
    [DEV_CMD_START_WASH]       = DEV_CMD_EFFECT_START_WASH,
    [DEV_CMD_STOP_WASH]        = DEV_CMD_EFFECT_STOP_WASH,
    [DEV_CMD_START_SELF_CHECK] = DEV_CMD_EFFECT_SELF_CHECK,
    [DEV_CMD_MANUAL_ACTUATOR]  = DEV_CMD_EFFECT_MANUAL_ACTUATOR,
    [DEV_CMD_STOP_ALL_OUTPUTS] = DEV_CMD_EFFECT_STOP_ALL_OUTPUTS,
    /* 其余默认 DEV_CMD_EFFECT_NONE = 0 */
};

static dev_cmd_decision_t check_command(const dev_cmd_t *cmd)
{
    dev_cmd_kind_t kind;
    op_perm_t      perm;

    if ((cmd == NULL) || (cmd->body.kind <= DEV_CMD_NONE) || (cmd->body.kind >= DEV_CMD_MAX)) {
        return make_denied(OP_REJECT_UNKNOWN_CMD);
    }

    kind = cmd->body.kind;

    if ((s_mode < OP_MODE_INIT) || (s_mode > OP_MODE_RECOVERING)) {
        return make_denied(OP_REJECT_WRONG_MODE);
    }

    perm = k_cmd_matrix[kind][s_mode];
    if (perm == OP_PERM_DENIED) {
        return make_denied(OP_REJECT_WRONG_MODE);
    }

    /* CONDITIONAL 命令：首先检查急停 */
    if (perm == OP_PERM_CONDITIONAL) {
        if (s_estop_active) {
            return make_denied(OP_REJECT_ESTOP_ACTIVE);
        }
    }

    /* 各命令专属运行时条件 */
    if (kind == DEV_CMD_START_WASH) {
        const machine_ops_t *ops;

        if (!s_service_enabled) {
            return make_denied(OP_REJECT_SERVICE_DISABLED);
        }
        if (alarm_registry_has_blocking_active()) {
            return make_denied(OP_REJECT_WRONG_MODE);
        }
        ops = machine_ops_get();
        if ((ops != NULL) && (ops->is_wash_entry_ready != NULL) && !ops->is_wash_entry_ready()) {
            return make_denied(OP_REJECT_VEHICLE_NOT_READY);
        }
    }

    if (kind == DEV_CMD_RECOVER) {
        /* 运营总开关关闭时禁止归位和故障恢复。 */
        if (!s_service_enabled) {
            return make_denied(OP_REJECT_SERVICE_DISABLED);
        }
    }

    if (kind == DEV_CMD_RESUME_OPERATION) {
        /* 只在 service 已停止时才允许 RESUME */
        if (s_service_enabled) {
            return make_denied(OP_REJECT_WRONG_MODE);
        }
    }

    return (dev_cmd_decision_t){
        .verdict        = OP_CMD_ALLOWED,
        .reason         = OP_REJECT_NONE,
        .pending_effect = k_cmd_effects[kind],
    };
}

sw_err_t operational_mode_init(void)
{
    s_mode            = OP_MODE_STOPPED;
    s_estop_active    = false;
    s_service_enabled = true;
    LOG_INFO("operational_mode: init ok (STOPPED)");
    return SW_OK;
}

dev_cmd_decision_t op_mode_handle_command(const dev_cmd_t *cmd)
{
    dev_cmd_decision_t d;
    dev_cmd_kind_t     kind;

    d = check_command(cmd);
    if (d.verdict != OP_CMD_ALLOWED) {
        if (cmd != NULL) {
            LOG_WARN("op_mode: cmd %d rejected (reason=%d, mode=%s)",
                     (int)cmd->body.kind,
                     (int)d.reason,
                     op_mode_name(s_mode));
            /* 拒绝结果由 command_gateway 统一发 EVT_OP_MODE_CMD_HANDLED，此处不另发事件 */
        }
        return d;
    }

    kind = cmd->body.kind;

    switch (kind) {
    case DEV_CMD_START_SELF_CHECK:
        set_mode(OP_MODE_SELF_CHECK, NULL);
        break;

    case DEV_CMD_RECOVER:
        /* STOPPED 与 EXCEPTION 统一走 recovery_service（复位锁存告警 + 异步归位）*/
        if ((s_mode == OP_MODE_STOPPED) || (s_mode == OP_MODE_EXCEPTION)) {
            set_mode(OP_MODE_RECOVERING, NULL);
            d.pending_effect = DEV_CMD_EFFECT_NONE;
            (void)event_publish(EVT_OP_MODE_RECOVERY_REQUESTED, 0U);
        }
        break;

    case DEV_CMD_STOP_OPERATION:
        /* 不运营 = 停机：关总开关并离开 IDLE/WASH_DONE */
        op_mode_set_service_enabled(false);
        set_mode(OP_MODE_STOPPED, "stop operation");
        break;

    case DEV_CMD_RESUME_OPERATION:
        /* 仅重新授权；模式保持 STOPPED，须 HOME 后才进 IDLE */
        op_mode_set_service_enabled(true);
        break;

    case DEV_CMD_START_WASH:
    case DEV_CMD_STOP_WASH:
    case DEV_CMD_MANUAL_ACTUATOR:
    case DEV_CMD_STOP_ALL_OUTPUTS:
    default:
        break;
    }

    return d;
}

void op_mode_on_wash_session_started(void)
{
    if (s_mode == OP_MODE_IDLE) {
        set_mode(OP_MODE_WASHING, NULL);
    }
}

void op_mode_on_wash_session_completed(void)
{
    if (s_mode != OP_MODE_WASHING) {
        return;
    }

    /* MAJOR+：洗中已允许跑完；结束时若仍活跃则停机，不再进 WASH_DONE */
    if (alarm_registry_has_blocking_active()) {
        set_mode(OP_MODE_EXCEPTION, "post-wash major still active");
        return;
    }

    set_mode(OP_MODE_WASH_DONE, NULL);
}

void op_mode_on_wash_session_aborted(wash_abort_cause_t cause)
{
    if (s_mode != OP_MODE_WASHING) {
        /* 急停路径：on_estop_triggered 已切换至 EXCEPTION，此处忽略 */
        return;
    }

    /* 所有非急停中止原因均进入中止归位，防止机构阻碍客户离开 */
    set_mode(OP_MODE_ABORT_HOMING, wash_abort_name(cause));
    (void)event_publish(EVT_ABORT_HOME_REQUESTED, 0U);
}

void op_mode_on_wash_customer_gone(void)
{
    if (s_mode == OP_MODE_WASH_DONE) {
        set_mode(OP_MODE_IDLE, NULL);
    }
}

void op_mode_on_self_check_completed(bool land_exception)
{
    if (s_mode != OP_MODE_SELF_CHECK) {
        return;
    }

    if (land_exception) {
        set_mode(OP_MODE_EXCEPTION, "self-check failed");
    } else {
        set_mode(OP_MODE_STOPPED, NULL);
    }
}

void op_mode_on_critical_alarm(void)
{
    if ((s_mode != OP_MODE_WASHING) && (s_mode != OP_MODE_ABORT_HOMING) && (s_mode != OP_MODE_RECOVERING)) {
        set_mode(OP_MODE_EXCEPTION, "critical alarm");
    }
}

void op_mode_on_blocking_alarm(void)
{
    /* 运行中的动作先按既定流程安全结束，静态状态立即进入故障停机。 */
    if ((s_mode != OP_MODE_WASHING) && (s_mode != OP_MODE_HOMING) && (s_mode != OP_MODE_ABORT_HOMING)
        && (s_mode != OP_MODE_RECOVERING)) {
        set_mode(OP_MODE_EXCEPTION, "blocking alarm");
    }
}

void op_mode_on_estop(bool active)
{
    s_estop_active = active;
    if (active) {
        set_mode(OP_MODE_EXCEPTION, "estop");
    } else {
        publish_context_sync();
    }
}

void op_mode_on_recovery_completed(recovery_result_t result)
{
    if (s_mode != OP_MODE_RECOVERING) {
        return;
    }

    if (result == RECOVERY_RESULT_IDLE) {
        set_mode(OP_MODE_IDLE, NULL);
    } else {
        set_mode(OP_MODE_EXCEPTION, "recovery failed");
    }
}

void op_mode_on_home_done(bool success)
{
    if (s_mode == OP_MODE_HOMING) {
        if (!success || alarm_registry_has_blocking_active()) {
            set_mode(OP_MODE_EXCEPTION, success ? "blocking alarm after home" : "home failed");
        } else {
            set_mode(OP_MODE_IDLE, NULL);
        }
    } else if (s_mode == OP_MODE_ABORT_HOMING) {
        set_mode(OP_MODE_EXCEPTION, NULL);
    }
}

operational_mode_t op_mode_get_current(void)
{
    return s_mode;
}

bool op_mode_is_estop_active(void)
{
    return s_estop_active;
}

bool op_mode_is_service_enabled(void)
{
    return s_service_enabled;
}

bool op_mode_is_stopping(void)
{
    /* 非运营接单态：停机/归位中/故障处理/中止清障等 */
    return (s_mode == OP_MODE_INIT) || (s_mode == OP_MODE_STOPPED) || (s_mode == OP_MODE_HOMING)
           || (s_mode == OP_MODE_EXCEPTION) || (s_mode == OP_MODE_RECOVERING) || (s_mode == OP_MODE_ABORT_HOMING)
           || (s_mode == OP_MODE_SELF_CHECK) || !s_service_enabled;
}

bool op_mode_is_standby(void)
{
    /* IDLE 蕴含 service_enabled（STOP_OPERATION 会离开 IDLE）*/
    return s_mode == OP_MODE_IDLE;
}

static void op_mode_set_service_enabled(bool enabled)
{
    s_service_enabled = enabled;
    publish_context_sync();
}
