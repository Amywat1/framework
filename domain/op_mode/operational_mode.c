/**
 * @file    operational_mode.c
 * @brief   OperationalMode 聚合根实现（8 态状态机，表驱动）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "domain/op_mode/operational_mode.h"

#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_mutex.h"
#include "domain/ports/outbound/machine/machine_ops_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "runtime/event_bus/event_bus.h"

#include <pthread.h>

typedef enum {
    OP_PERM_DENIED = 0,
    OP_PERM_ALLOWED,
    OP_PERM_CONDITIONAL, /**< 允许，但需通过运行时急停检查 */
} op_perm_t;

static operational_mode_t s_mode            = OP_MODE_INIT;
static bool               s_estop_active    = false;
static bool               s_service_enabled = true;
static pthread_mutex_t    s_mu;
static pthread_once_t     s_mu_once = PTHREAD_ONCE_INIT;

static void op_mode_mu_init(void)
{
    (void)sw_mutex_init_prio_inherit(&s_mu);
}

static void op_mode_lock(void)
{
    (void)pthread_once(&s_mu_once, op_mode_mu_init);
    (void)pthread_mutex_lock(&s_mu);
}

static void op_mode_unlock(void)
{
    (void)pthread_mutex_unlock(&s_mu);
}

static void op_mode_set_service_enabled(bool enabled);

static const char *op_mode_name(operational_mode_t mode)
{
    switch (mode) {
    case OP_MODE_INIT:
        return "INIT";
    case OP_MODE_STOPPED:
        return "STOPPED";
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
    case WASH_ABORT_STOP_ALL:
        return "stop_all";
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

    /*
     * IDLE 不变量：总开关开且无急停；破坏时降级 STOPPED，避免静默接单。
     * 当前公开路径不可达（进 IDLE 前急停/关总开关已离可进态）；纯防御——
     * 新增进 IDLE 转移时由此兜住，勿当死代码删除。
     */
    if ((next == OP_MODE_IDLE) && (!s_service_enabled || s_estop_active)) {
        LOG_ERROR("op_mode: refuse IDLE (service_enabled=%d estop=%d) -> STOPPED",
                  (int)s_service_enabled,
                  (int)s_estop_active);
        next  = OP_MODE_STOPPED;
        cause = "idle invariant";
    }

    if (s_mode == next) {
        return;
    }

    from   = s_mode;
    level  = (next == OP_MODE_ABORT_HOMING) ? SW_LOG_WARN : SW_LOG_INFO;
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
 *   INIT, STOPPED, IDLE, WASHING, ABORT_HOMING, WASH_DONE, SELF_CHECK, RECOVERING
 *
 * CONDITIONAL：允许但需后续检查急停（运营开关等专属条件见 check_command）
 */
static const op_perm_t k_cmd_matrix[DEV_CMD_MAX][OP_MODE_RECOVERING + 1] =
{
    /* DEV_CMD_START_WASH：仅 IDLE；标 C 以防不变量被未来进 IDLE 路径破坏 */
    [DEV_CMD_START_WASH] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_DENIED,      /* STOPPED     */
        OP_PERM_CONDITIONAL, /* IDLE        */
        OP_PERM_DENIED,      /* WASHING     */
        OP_PERM_DENIED,      /* ABORT_HOMING*/
        OP_PERM_DENIED,      /* WASH_DONE   */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    /* DEV_CMD_STOP_WASH：仅 WASHING */
    [DEV_CMD_STOP_WASH] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_DENIED,      /* STOPPED     */
        OP_PERM_DENIED,      /* IDLE        */
        OP_PERM_ALLOWED,     /* WASHING     */
        OP_PERM_DENIED,      /* ABORT_HOMING*/
        OP_PERM_DENIED,      /* WASH_DONE   */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    /* DEV_CMD_STOP_OPERATION：非自动化态关总开关并落到 STOPPED；洗车/自检/恢复/清障中拒绝 */
    [DEV_CMD_STOP_OPERATION] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_ALLOWED,     /* STOPPED     */
        OP_PERM_ALLOWED,     /* IDLE        */
        OP_PERM_DENIED,      /* WASHING     */
        OP_PERM_DENIED,      /* ABORT_HOMING*/
        OP_PERM_ALLOWED,     /* WASH_DONE   */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    /* DEV_CMD_RESUME_OPERATION：仅翻总开关，与模式无关；已开则幂等 */
    [DEV_CMD_RESUME_OPERATION] = {
        OP_PERM_DENIED,  /* INIT        */
        OP_PERM_ALLOWED, /* STOPPED     */
        OP_PERM_ALLOWED, /* IDLE        */
        OP_PERM_ALLOWED, /* WASHING     */
        OP_PERM_ALLOWED, /* ABORT_HOMING*/
        OP_PERM_ALLOWED, /* WASH_DONE   */
        OP_PERM_ALLOWED, /* SELF_CHECK  */
        OP_PERM_ALLOWED, /* RECOVERING  */
    },
    /* DEV_CMD_MANUAL_ACTUATOR：仅 STOPPED；急停激活时拒绝 */
    [DEV_CMD_MANUAL_ACTUATOR] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_CONDITIONAL, /* STOPPED     */
        OP_PERM_DENIED,      /* IDLE        */
        OP_PERM_DENIED,      /* WASHING     */
        OP_PERM_DENIED,      /* ABORT_HOMING*/
        OP_PERM_DENIED,      /* WASH_DONE   */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    /* DEV_CMD_START_SELF_CHECK：仅 STOPPED；急停激活时拒绝（LOCKOUT/blocking 允许）*/
    [DEV_CMD_START_SELF_CHECK] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_CONDITIONAL, /* STOPPED     */
        OP_PERM_DENIED,      /* IDLE        */
        OP_PERM_DENIED,      /* WASHING     */
        OP_PERM_DENIED,      /* ABORT_HOMING*/
        OP_PERM_DENIED,      /* WASH_DONE   */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    /* DEV_CMD_RECOVER：STOPPED 走恢复；IDLE 幂等成功 */
    [DEV_CMD_RECOVER] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_CONDITIONAL, /* STOPPED     */
        OP_PERM_ALLOWED,     /* IDLE        */
        OP_PERM_DENIED,      /* WASHING     */
        OP_PERM_DENIED,      /* ABORT_HOMING*/
        OP_PERM_DENIED,      /* WASH_DONE   */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    /* DEV_CMD_STOP_ALL_OUTPUTS：除 INIT 外全面收敛到 STOPPED */
    [DEV_CMD_STOP_ALL_OUTPUTS] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_ALLOWED,     /* STOPPED     */
        OP_PERM_ALLOWED,     /* IDLE        */
        OP_PERM_ALLOWED,     /* WASHING     */
        OP_PERM_ALLOWED,     /* ABORT_HOMING*/
        OP_PERM_ALLOWED,     /* WASH_DONE   */
        OP_PERM_ALLOWED,     /* SELF_CHECK  */
        OP_PERM_ALLOWED,     /* RECOVERING  */
    },
};

static dev_cmd_decision_t make_denied(op_reject_reason_t reason)
{
    return (dev_cmd_decision_t){
        .verdict = OP_CMD_DENIED,
        .reason  = reason,
    };
}

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

    return (dev_cmd_decision_t){
        .verdict = OP_CMD_ALLOWED,
        .reason  = OP_REJECT_NONE,
    };
}

sw_err_t operational_mode_init(void)
{
    op_mode_lock();
    s_mode            = OP_MODE_STOPPED;
    s_estop_active    = false;
    s_service_enabled = true;
    op_mode_unlock();
    LOG_INFO("operational_mode: init ok (STOPPED)");
    return SW_OK;
}

dev_cmd_decision_t op_mode_handle_command(const dev_cmd_t *cmd)
{
    dev_cmd_decision_t d;
    dev_cmd_kind_t     kind;
    operational_mode_t mode_before;

    op_mode_lock();
    mode_before   = s_mode;
    d             = check_command(cmd);
    d.mode_before = mode_before;
    if (d.verdict != OP_CMD_ALLOWED) {
        if (cmd != NULL) {
            LOG_WARN("op_mode: cmd %d rejected (reason=%d, mode=%s)",
                     (int)cmd->body.kind,
                     (int)d.reason,
                     op_mode_name(s_mode));
        }
        op_mode_unlock();
        return d;
    }

    kind = cmd->body.kind;

    switch (kind) {
    case DEV_CMD_START_SELF_CHECK:
        set_mode(OP_MODE_SELF_CHECK, NULL);
        break;

    case DEV_CMD_RECOVER:
        /* STOPPED 走 recovery_service；IDLE 幂等 */
        if (s_mode == OP_MODE_STOPPED) {
            set_mode(OP_MODE_RECOVERING, NULL);
            (void)event_publish(EVT_OP_MODE_RECOVERY_REQUESTED, 0U);
        }
        break;

    case DEV_CMD_STOP_OPERATION:
        /* 关总开关并离开 IDLE/WASH_DONE */
        op_mode_set_service_enabled(false);
        set_mode(OP_MODE_STOPPED, "stop operation");
        break;

    case DEV_CMD_RESUME_OPERATION:
        /* 仅重新授权（已开则幂等）；接单仍须 RECOVER → IDLE */
        op_mode_set_service_enabled(true);
        break;

    case DEV_CMD_STOP_ALL_OUTPUTS:
        /* 先切 STOPPED 再跑 router，避免误入 ABORT_HOMING；见命令网关设计 §5.2 观测窗口 */
        set_mode(OP_MODE_STOPPED, "stop all outputs");
        break;

    case DEV_CMD_START_WASH:
    case DEV_CMD_STOP_WASH:
    case DEV_CMD_MANUAL_ACTUATOR:
    default:
        break;
    }

    op_mode_unlock();
    return d;
}

void op_mode_on_wash_session_started(void)
{
    op_mode_lock();
    if (s_mode == OP_MODE_IDLE) {
        set_mode(OP_MODE_WASHING, NULL);
    }
    op_mode_unlock();
}

void op_mode_on_wash_session_completed(void)
{
    op_mode_lock();
    if (s_mode != OP_MODE_WASHING) {
        op_mode_unlock();
        return;
    }

    if (alarm_registry_has_blocking_active()) {
        set_mode(OP_MODE_STOPPED, "post-wash major still active");
        op_mode_unlock();
        return;
    }

    set_mode(OP_MODE_WASH_DONE, NULL);
    op_mode_unlock();
}

void op_mode_on_wash_session_aborted(wash_abort_cause_t cause)
{
    op_mode_lock();
    if (s_mode != OP_MODE_WASHING) {
        op_mode_unlock();
        return;
    }

    set_mode(OP_MODE_ABORT_HOMING, wash_abort_name(cause));
    (void)event_publish(EVT_ABORT_HOME_REQUESTED, 0U);
    op_mode_unlock();
}

void op_mode_on_wash_customer_gone(void)
{
    op_mode_lock();
    if (s_mode == OP_MODE_WASH_DONE) {
        set_mode(OP_MODE_IDLE, NULL);
    }
    op_mode_unlock();
}

void op_mode_on_self_check_completed(bool land_fault)
{
    op_mode_lock();
    if (s_mode != OP_MODE_SELF_CHECK) {
        op_mode_unlock();
        return;
    }

    set_mode(OP_MODE_STOPPED, land_fault ? "self-check failed" : NULL);
    op_mode_unlock();
}

void op_mode_on_critical_alarm(void)
{
    op_mode_lock();
    if ((s_mode != OP_MODE_WASHING) && (s_mode != OP_MODE_ABORT_HOMING) && (s_mode != OP_MODE_RECOVERING)) {
        set_mode(OP_MODE_STOPPED, "critical alarm");
    }
    op_mode_unlock();
}

void op_mode_on_blocking_alarm(void)
{
    op_mode_lock();
    if ((s_mode != OP_MODE_WASHING) && (s_mode != OP_MODE_ABORT_HOMING) && (s_mode != OP_MODE_RECOVERING)) {
        set_mode(OP_MODE_STOPPED, "blocking alarm");
    }
    op_mode_unlock();
}

void op_mode_on_estop(bool active)
{
    op_mode_lock();
    if (s_estop_active == active) {
        op_mode_unlock();
        return;
    }

    s_estop_active = active;
    if (active) {
        set_mode(OP_MODE_STOPPED, "estop");
    }
    /* 旗标变化一律刷上下文：已 STOPPED 时 set_mode 不发 CHANGED，须靠本事件刷新快照 */
    publish_context_sync();
    op_mode_unlock();
}

void op_mode_on_recovery_completed(recovery_result_t result)
{
    op_mode_lock();
    if (s_mode != OP_MODE_RECOVERING) {
        op_mode_unlock();
        return;
    }

    if (result == RECOVERY_RESULT_IDLE) {
        set_mode(OP_MODE_IDLE, NULL);
    } else {
        set_mode(OP_MODE_STOPPED, "recovery failed");
    }
    op_mode_unlock();
}

void op_mode_on_home_done(void)
{
    op_mode_lock();
    if (s_mode == OP_MODE_ABORT_HOMING) {
        set_mode(OP_MODE_STOPPED, NULL);
    }
    op_mode_unlock();
}

op_cmd_perm_t op_mode_cmd_matrix_perm(dev_cmd_kind_t kind, operational_mode_t mode)
{
    if ((kind <= DEV_CMD_NONE) || (kind >= DEV_CMD_MAX) || (mode < OP_MODE_INIT) || (mode > OP_MODE_RECOVERING)) {
        return OP_CMD_PERM_DENIED;
    }

    switch (k_cmd_matrix[kind][mode]) {
    case OP_PERM_ALLOWED:
        return OP_CMD_PERM_ALLOWED;
    case OP_PERM_CONDITIONAL:
        return OP_CMD_PERM_CONDITIONAL;
    case OP_PERM_DENIED:
    default:
        return OP_CMD_PERM_DENIED;
    }
}

operational_mode_t op_mode_get_current(void)
{
    operational_mode_t mode;

    op_mode_lock();
    mode = s_mode;
    op_mode_unlock();
    return mode;
}

bool op_mode_is_estop_active(void)
{
    bool active;

    op_mode_lock();
    active = s_estop_active;
    op_mode_unlock();
    return active;
}

bool op_mode_is_service_enabled(void)
{
    bool enabled;

    op_mode_lock();
    enabled = s_service_enabled;
    op_mode_unlock();
    return enabled;
}

bool op_mode_is_stopping(void)
{
    bool stopping;

    op_mode_lock();
    stopping = (s_mode == OP_MODE_INIT) || (s_mode == OP_MODE_STOPPED) || (s_mode == OP_MODE_RECOVERING)
               || (s_mode == OP_MODE_ABORT_HOMING) || (s_mode == OP_MODE_SELF_CHECK) || !s_service_enabled;
    op_mode_unlock();
    return stopping;
}

bool op_mode_is_standby(void)
{
    bool standby;

    op_mode_lock();
    standby = (s_mode == OP_MODE_IDLE);
    op_mode_unlock();
    return standby;
}

static void op_mode_set_service_enabled(bool enabled)
{
    if (s_service_enabled == enabled) {
        return;
    }

    s_service_enabled = enabled;
    publish_context_sync();
}
