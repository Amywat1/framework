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
#include "domain/job/machine_profile.h"
#include "domain/ports/outbound/safety/safety_port.h"
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

static const char *op_mode_name(operational_mode_t mode)
{
    switch (mode) {
    case OP_MODE_INIT:
        return "INIT";
    case OP_MODE_STOPPED:
        return "STOPPED";
    case OP_MODE_STANDBY:
        return "STANDBY";
    case OP_MODE_ACTIVE:
        return "ACTIVE";
    case OP_MODE_CLEARING:
        return "CLEARING";
    case OP_MODE_HOLD:
        return "HOLD";
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
    sw_err_t ret = event_publish(EVT_OP_MODE_CHANGED, op_mode_changed_evt_param(from, to));

    if (ret != SW_OK) {
        LOG_WARN("op_mode: mode projection event dropped ret=%d", (int)ret);
    }
}

static void publish_context_sync(void)
{
    sw_err_t ret = event_publish(EVT_OP_MODE_CONTEXT_SYNC, 0U);

    if (ret != SW_OK) {
        LOG_WARN("op_mode: context projection event dropped ret=%d", (int)ret);
    }
}

static void set_mode(operational_mode_t next, const char *cause)
{
    operational_mode_t from;

    /* 勿当死代码删除：STANDBY 必须总开关开且无急停，否则降级 STOPPED。 */
    if ((next == OP_MODE_STANDBY) && (!s_service_enabled || s_estop_active)) {
        LOG_ERROR("op_mode: refuse STANDBY (service_enabled=%d estop=%d) -> STOPPED",
                  (int)s_service_enabled,
                  (int)s_estop_active);
        next  = OP_MODE_STOPPED;
        cause = "standby invariant";
    }

    if (s_mode == next) {
        return;
    }

    from   = s_mode;
    s_mode = next;

    if (next == OP_MODE_CLEARING) {
        if (cause != NULL) {
            LOG_WARN("op_mode: %s -> %s (%s)", op_mode_name(from), op_mode_name(next), cause);
        } else {
            LOG_WARN("op_mode: %s -> %s", op_mode_name(from), op_mode_name(next));
        }
    } else if (cause != NULL) {
        LOG_INFO("op_mode: %s -> %s (%s)", op_mode_name(from), op_mode_name(next), cause);
    } else {
        LOG_INFO("op_mode: %s -> %s", op_mode_name(from), op_mode_name(next));
    }

    publish_mode_changed(from, next);
}

/* 写入运营总开关；停业且当前 STANDBY/HOLD 时落到 STOPPED */
static void apply_service_enabled_locked(bool enabled)
{
    if (s_service_enabled != enabled) {
        s_service_enabled = enabled;
        publish_context_sync();
    }
    if (!s_service_enabled && ((s_mode == OP_MODE_STANDBY) || (s_mode == OP_MODE_HOLD))) {
        set_mode(OP_MODE_STOPPED, "service disabled");
    }
}

/*
 * 命令权限矩阵
 *
 * 列顺序与 operational_mode_t 枚举一致：
 *   INIT, STOPPED, STANDBY, ACTIVE, CLEARING, HOLD, SELF_CHECK, RECOVERING
 */
static const op_perm_t k_cmd_matrix[DEV_CMD_MAX][OP_MODE_RECOVERING + 1] =
{
    [DEV_CMD_SUBMIT_ORDER] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_CONDITIONAL, /* STOPPED     */
        OP_PERM_CONDITIONAL, /* STANDBY     */
        OP_PERM_CONDITIONAL, /* ACTIVE      */
        OP_PERM_CONDITIONAL, /* CLEARING    */
        OP_PERM_CONDITIONAL, /* HOLD        */
        OP_PERM_CONDITIONAL, /* SELF_CHECK  */
        OP_PERM_CONDITIONAL, /* RECOVERING  */
    },
    [DEV_CMD_CLEAR_MACHINE] = {
        OP_PERM_DENIED,  /* INIT        */
        OP_PERM_DENIED,  /* STOPPED     */
        OP_PERM_DENIED,  /* STANDBY     */
        OP_PERM_ALLOWED, /* ACTIVE      */
        OP_PERM_DENIED,  /* CLEARING    */
        OP_PERM_DENIED,  /* HOLD        */
        OP_PERM_DENIED,  /* SELF_CHECK  */
        OP_PERM_DENIED,  /* RECOVERING  */
    },
    [DEV_CMD_SET_SERVICE] = {
        OP_PERM_DENIED,  /* INIT        */
        OP_PERM_ALLOWED, /* STOPPED     */
        OP_PERM_ALLOWED, /* STANDBY     */
        OP_PERM_ALLOWED, /* ACTIVE      */
        OP_PERM_ALLOWED, /* CLEARING    */
        OP_PERM_ALLOWED, /* HOLD        */
        OP_PERM_ALLOWED, /* SELF_CHECK  */
        OP_PERM_ALLOWED, /* RECOVERING  */
    },
    [DEV_CMD_MANUAL_ACTUATOR] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_CONDITIONAL, /* STOPPED     */
        OP_PERM_DENIED,      /* STANDBY     */
        OP_PERM_DENIED,      /* ACTIVE      */
        OP_PERM_DENIED,      /* CLEARING    */
        OP_PERM_DENIED,      /* HOLD        */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    [DEV_CMD_START_SELF_CHECK] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_CONDITIONAL, /* STOPPED     */
        OP_PERM_DENIED,      /* STANDBY     */
        OP_PERM_DENIED,      /* ACTIVE      */
        OP_PERM_DENIED,      /* CLEARING    */
        OP_PERM_DENIED,      /* HOLD        */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    [DEV_CMD_RECOVER] = {
        OP_PERM_DENIED,      /* INIT        */
        OP_PERM_CONDITIONAL, /* STOPPED     */
        OP_PERM_ALLOWED,     /* STANDBY     */
        OP_PERM_DENIED,      /* ACTIVE      */
        OP_PERM_DENIED,      /* CLEARING    */
        OP_PERM_DENIED,      /* HOLD        */
        OP_PERM_DENIED,      /* SELF_CHECK  */
        OP_PERM_DENIED,      /* RECOVERING  */
    },
    [DEV_CMD_STOP_ALL_OUTPUTS] = {
        OP_PERM_DENIED,  /* INIT        */
        OP_PERM_ALLOWED, /* STOPPED     */
        OP_PERM_ALLOWED, /* STANDBY     */
        OP_PERM_ALLOWED, /* ACTIVE      */
        OP_PERM_ALLOWED, /* CLEARING    */
        OP_PERM_ALLOWED, /* HOLD        */
        OP_PERM_ALLOWED, /* SELF_CHECK  */
        OP_PERM_ALLOWED, /* RECOVERING  */
    },
    [DEV_CMD_CANCEL_ORDER] = {
        OP_PERM_DENIED,  /* INIT        */
        OP_PERM_ALLOWED, /* STOPPED     */
        OP_PERM_ALLOWED, /* STANDBY     */
        OP_PERM_ALLOWED, /* ACTIVE      */
        OP_PERM_ALLOWED, /* CLEARING    */
        OP_PERM_ALLOWED, /* HOLD        */
        OP_PERM_ALLOWED, /* SELF_CHECK  */
        OP_PERM_ALLOWED, /* RECOVERING  */
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

    if (perm == OP_PERM_CONDITIONAL) {
        if (s_estop_active) {
            return make_denied(OP_REJECT_ESTOP_ACTIVE);
        }
    }

    if (kind == DEV_CMD_SUBMIT_ORDER) {
        if (!s_service_enabled) {
            return make_denied(OP_REJECT_SERVICE_DISABLED);
        }
        if (s_estop_active) {
            return make_denied(OP_REJECT_ESTOP_ACTIVE);
        }
        if (safety_cutout_is_unconfirmed()) {
            return make_denied(OP_REJECT_CUTOUT_UNCONFIRMED);
        }
    }

    if (kind == DEV_CMD_SET_SERVICE) {
        if (!cmd->body.payload.service.enabled) {
            if ((s_mode == OP_MODE_CLEARING) || (s_mode == OP_MODE_SELF_CHECK)
                || (s_mode == OP_MODE_RECOVERING)) {
                return make_denied(OP_REJECT_WRONG_MODE);
            }
        }
    }

    if (kind == DEV_CMD_RECOVER) {
        if (!s_service_enabled) {
            return make_denied(OP_REJECT_SERVICE_DISABLED);
        }
    }

    if ((kind == DEV_CMD_MANUAL_ACTUATOR) || (kind == DEV_CMD_START_SELF_CHECK)
        || (kind == DEV_CMD_RECOVER)) {
        if (safety_cutout_is_unconfirmed()) {
            return make_denied(OP_REJECT_CUTOUT_UNCONFIRMED);
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
        if (s_mode == OP_MODE_STOPPED) {
            set_mode(OP_MODE_RECOVERING, NULL);
            (void)event_publish_required(EVT_OP_MODE_RECOVERY_REQUESTED, 0U);
        }
        break;

    case DEV_CMD_SET_SERVICE:
        apply_service_enabled_locked(cmd->body.payload.service.enabled);
        break;

    case DEV_CMD_STOP_ALL_OUTPUTS:
        /* 必须先切 STOPPED 再让 application 跑切断/清簿副作用。 */
        set_mode(OP_MODE_STOPPED, "stop all outputs");
        break;

    case DEV_CMD_CLEAR_MACHINE:
        set_mode(OP_MODE_CLEARING, "clear machine");
        (void)event_publish_required(EVT_CLEAR_MACHINE_REQUESTED, 0U);
        break;

    case DEV_CMD_SUBMIT_ORDER:
    case DEV_CMD_CANCEL_ORDER:
    case DEV_CMD_MANUAL_ACTUATOR:
    default:
        break;
    }

    op_mode_unlock();
    return d;
}

void op_mode_on_job_started(void)
{
    op_mode_lock();
    if (s_mode == OP_MODE_STANDBY) {
        set_mode(OP_MODE_ACTIVE, NULL);
    }
    op_mode_unlock();
}

void op_mode_on_last_job_finished(void)
{
    machine_profile_t profile;

    op_mode_lock();
    if (s_mode != OP_MODE_ACTIVE) {
        op_mode_unlock();
        return;
    }

    if (alarm_registry_has_blocking_active()) {
        set_mode(OP_MODE_STOPPED, "post-job major still active");
        op_mode_unlock();
        return;
    }

    profile = machine_profile_get();
    if (profile.hold_after_last_job) {
        set_mode(OP_MODE_HOLD, NULL);
    } else {
        set_mode(OP_MODE_STANDBY, NULL);
    }
    op_mode_unlock();
}

void op_mode_on_clear_requested(wash_abort_cause_t cause)
{
    bool skip_clear;

    op_mode_lock();
    if (s_mode != OP_MODE_ACTIVE) {
        op_mode_unlock();
        return;
    }

    skip_clear = (cause == WASH_ABORT_ESTOP) || (cause == WASH_ABORT_STOP_ALL) || s_estop_active;
    if (skip_clear) {
        set_mode(OP_MODE_STOPPED, wash_abort_name(cause));
        op_mode_unlock();
        return;
    }

    set_mode(OP_MODE_CLEARING, wash_abort_name(cause));
    (void)event_publish_required(EVT_CLEAR_MACHINE_REQUESTED, 0U);
    op_mode_unlock();
}

void op_mode_on_area_cleared(void)
{
    op_mode_lock();
    if (s_mode == OP_MODE_HOLD) {
        set_mode(OP_MODE_STANDBY, NULL);
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

void op_mode_on_blocking_alarm(void)
{
    op_mode_lock();
    if ((s_mode != OP_MODE_ACTIVE) && (s_mode != OP_MODE_CLEARING) && (s_mode != OP_MODE_RECOVERING)) {
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

    if (result == RECOVERY_RESULT_STANDBY) {
        set_mode(OP_MODE_STANDBY, NULL);
    } else {
        set_mode(OP_MODE_STOPPED, "recovery failed");
    }
    op_mode_unlock();
}

void op_mode_on_clear_done(void)
{
    op_mode_lock();
    if (s_mode == OP_MODE_CLEARING) {
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

bool op_mode_can_admit(void)
{
    bool can;

    op_mode_lock();
    can = s_service_enabled && !s_estop_active
          && ((s_mode == OP_MODE_STANDBY) || (s_mode == OP_MODE_ACTIVE));
    op_mode_unlock();
    return can;
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
               || (s_mode == OP_MODE_CLEARING) || (s_mode == OP_MODE_SELF_CHECK) || !s_service_enabled;
    op_mode_unlock();
    return stopping;
}

bool op_mode_is_standby(void)
{
    bool standby;

    op_mode_lock();
    standby = (s_mode == OP_MODE_STANDBY);
    op_mode_unlock();
    return standby;
}

void op_mode_set_service_enabled(bool enabled)
{
    op_mode_lock();
    apply_service_enabled_locked(enabled);
    op_mode_unlock();
}
