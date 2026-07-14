/**
 * @file    operational_mode.c
 * @brief   OperationalMode 聚合根实现（表驱动）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "domain/command_gateway/operational_mode.h"

#include "common/event_types.h"
#include "common/log.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "runtime/event_bus/event_bus.h"

typedef enum {
    OP_PERM_DENIED = 0,
    OP_PERM_ALLOWED,
    OP_PERM_CONDITIONAL,
} op_perm_t;

static operational_mode_t s_mode            = OP_MODE_INIT;
static bool               s_estop_active    = false;
static bool               s_service_enabled = true;

static void publish_mode_changed(operational_mode_t from, operational_mode_t to)
{
    uint32_t param = ((uint32_t)from << 8) | (uint32_t)to;

    (void)event_publish(EVT_OP_MODE_CHANGED, param);
}

static void publish_cmd_rejected(dev_cmd_kind_t kind, op_reject_reason_t reason)
{
    uint32_t param = ((uint32_t)kind << 8) | (uint32_t)reason;

    (void)event_publish(EVT_OP_MODE_CMD_REJECTED, param);
}

static void publish_recovery_requested(void)
{
    (void)event_publish(EVT_OP_MODE_RECOVERY_REQUESTED, 0U);
}

static void publish_context_sync(void)
{
    (void)event_publish(EVT_OP_MODE_CONTEXT_SYNC, 0U);
}

static void set_mode(operational_mode_t next)
{
    if (s_mode == next) {
        return;
    }

    operational_mode_t from = s_mode;

    s_mode = next;
    LOG_INFO("op_mode: %d -> %d", (int)from, (int)next);
    publish_mode_changed(from, next);
}

static const op_perm_t k_cmd_matrix[DEV_CMD_MAX][OP_MODE_RECOVERING + 1] =
{
    /* DEV_CMD_NONE */
    [DEV_CMD_NONE] = {
        [OP_MODE_INIT]        = OP_PERM_DENIED,
        [OP_MODE_IDLE]        = OP_PERM_DENIED,
        [OP_MODE_WASHING]     = OP_PERM_DENIED,
        [OP_MODE_MANUAL]      = OP_PERM_DENIED,
        [OP_MODE_SELF_CHECK]  = OP_PERM_DENIED,
        [OP_MODE_EXCEPTION]   = OP_PERM_DENIED,
        [OP_MODE_RECOVERING]  = OP_PERM_DENIED,
    },
    /* DEV_CMD_START_WASH */
    [DEV_CMD_START_WASH] = {
        [OP_MODE_INIT]        = OP_PERM_DENIED,
        [OP_MODE_IDLE]        = OP_PERM_ALLOWED,
        [OP_MODE_WASHING]     = OP_PERM_DENIED,
        [OP_MODE_MANUAL]      = OP_PERM_DENIED,
        [OP_MODE_SELF_CHECK]  = OP_PERM_DENIED,
        [OP_MODE_EXCEPTION]   = OP_PERM_DENIED,
        [OP_MODE_RECOVERING]  = OP_PERM_DENIED,
    },
    /* DEV_CMD_STOP_WASH */
    [DEV_CMD_STOP_WASH] = {
        [OP_MODE_INIT]        = OP_PERM_DENIED,
        [OP_MODE_IDLE]        = OP_PERM_DENIED,
        [OP_MODE_WASHING]     = OP_PERM_ALLOWED,
        [OP_MODE_MANUAL]      = OP_PERM_DENIED,
        [OP_MODE_SELF_CHECK]  = OP_PERM_DENIED,
        [OP_MODE_EXCEPTION]   = OP_PERM_DENIED,
        [OP_MODE_RECOVERING]  = OP_PERM_DENIED,
    },
    /* DEV_CMD_STOP_OPERATION */
    [DEV_CMD_STOP_OPERATION] = {
        [OP_MODE_INIT]        = OP_PERM_DENIED,
        [OP_MODE_IDLE]        = OP_PERM_ALLOWED,
        [OP_MODE_WASHING]     = OP_PERM_DENIED,
        [OP_MODE_MANUAL]      = OP_PERM_DENIED,
        [OP_MODE_SELF_CHECK]  = OP_PERM_DENIED,
        [OP_MODE_EXCEPTION]   = OP_PERM_DENIED,
        [OP_MODE_RECOVERING]  = OP_PERM_DENIED,
    },
    /* DEV_CMD_RESUME_OPERATION */
    [DEV_CMD_RESUME_OPERATION] = {
        [OP_MODE_INIT]        = OP_PERM_DENIED,
        [OP_MODE_IDLE]        = OP_PERM_ALLOWED,
        [OP_MODE_WASHING]     = OP_PERM_DENIED,
        [OP_MODE_MANUAL]      = OP_PERM_DENIED,
        [OP_MODE_SELF_CHECK]  = OP_PERM_DENIED,
        [OP_MODE_EXCEPTION]   = OP_PERM_DENIED,
        [OP_MODE_RECOVERING]  = OP_PERM_DENIED,
    },
    /* DEV_CMD_RESET_FAULT */
    [DEV_CMD_RESET_FAULT] = {
        [OP_MODE_INIT]        = OP_PERM_DENIED,
        [OP_MODE_IDLE]        = OP_PERM_CONDITIONAL,
        [OP_MODE_WASHING]     = OP_PERM_DENIED,
        [OP_MODE_MANUAL]      = OP_PERM_DENIED,
        [OP_MODE_SELF_CHECK]  = OP_PERM_DENIED,
        [OP_MODE_EXCEPTION]   = OP_PERM_CONDITIONAL,
        [OP_MODE_RECOVERING]  = OP_PERM_DENIED,
    },
    /* DEV_CMD_HOME_DEVICE */
    [DEV_CMD_HOME_DEVICE] = {
        [OP_MODE_INIT]        = OP_PERM_DENIED,
        [OP_MODE_IDLE]        = OP_PERM_DENIED,
        [OP_MODE_WASHING]     = OP_PERM_DENIED,
        [OP_MODE_MANUAL]      = OP_PERM_DENIED,
        [OP_MODE_SELF_CHECK]  = OP_PERM_DENIED,
        [OP_MODE_EXCEPTION]   = OP_PERM_ALLOWED,
        [OP_MODE_RECOVERING]  = OP_PERM_DENIED,
    },
    /* DEV_CMD_ENTER_MANUAL */
    [DEV_CMD_ENTER_MANUAL] = {
        [OP_MODE_INIT]        = OP_PERM_DENIED,
        [OP_MODE_IDLE]        = OP_PERM_ALLOWED,
        [OP_MODE_WASHING]     = OP_PERM_DENIED,
        [OP_MODE_MANUAL]      = OP_PERM_DENIED,
        [OP_MODE_SELF_CHECK]  = OP_PERM_DENIED,
        [OP_MODE_EXCEPTION]   = OP_PERM_DENIED,
        [OP_MODE_RECOVERING]  = OP_PERM_DENIED,
    },
    /* DEV_CMD_MANUAL_ACTUATOR */
    [DEV_CMD_MANUAL_ACTUATOR] = {
        [OP_MODE_INIT]        = OP_PERM_DENIED,
        [OP_MODE_IDLE]        = OP_PERM_DENIED,
        [OP_MODE_WASHING]     = OP_PERM_DENIED,
        [OP_MODE_MANUAL]      = OP_PERM_ALLOWED,
        [OP_MODE_SELF_CHECK]  = OP_PERM_DENIED,
        [OP_MODE_EXCEPTION]   = OP_PERM_ALLOWED,
        [OP_MODE_RECOVERING]  = OP_PERM_DENIED,
    },
    /* DEV_CMD_START_SELF_CHECK */
    [DEV_CMD_START_SELF_CHECK] = {
        [OP_MODE_INIT]        = OP_PERM_DENIED,
        [OP_MODE_IDLE]        = OP_PERM_CONDITIONAL,
        [OP_MODE_WASHING]     = OP_PERM_DENIED,
        [OP_MODE_MANUAL]      = OP_PERM_DENIED,
        [OP_MODE_SELF_CHECK]  = OP_PERM_DENIED,
        [OP_MODE_EXCEPTION]   = OP_PERM_CONDITIONAL,
        [OP_MODE_RECOVERING]  = OP_PERM_DENIED,
    },
    /* DEV_CMD_RECOVER */
    [DEV_CMD_RECOVER] = {
        [OP_MODE_INIT]        = OP_PERM_DENIED,
        [OP_MODE_IDLE]        = OP_PERM_DENIED,
        [OP_MODE_WASHING]     = OP_PERM_DENIED,
        [OP_MODE_MANUAL]      = OP_PERM_CONDITIONAL,
        [OP_MODE_SELF_CHECK]  = OP_PERM_DENIED,
        [OP_MODE_EXCEPTION]   = OP_PERM_CONDITIONAL,
        [OP_MODE_RECOVERING]  = OP_PERM_DENIED,
    },
    /* DEV_CMD_STOP_ALL_OUTPUTS */
    [DEV_CMD_STOP_ALL_OUTPUTS] = {
        [OP_MODE_INIT]        = OP_PERM_DENIED,
        [OP_MODE_IDLE]        = OP_PERM_ALLOWED,
        [OP_MODE_WASHING]     = OP_PERM_ALLOWED,
        [OP_MODE_MANUAL]      = OP_PERM_ALLOWED,
        [OP_MODE_SELF_CHECK]  = OP_PERM_DENIED,
        [OP_MODE_EXCEPTION]   = OP_PERM_ALLOWED,
        [OP_MODE_RECOVERING]  = OP_PERM_ALLOWED,
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

static dev_cmd_effect_t effect_for_kind(dev_cmd_kind_t kind)
{
    switch (kind) {
    case DEV_CMD_START_WASH:
        return DEV_CMD_EFFECT_START_WASH;
    case DEV_CMD_STOP_WASH:
        return DEV_CMD_EFFECT_STOP_WASH;
    case DEV_CMD_START_SELF_CHECK:
        return DEV_CMD_EFFECT_SELF_CHECK;
    case DEV_CMD_RESET_FAULT:
        return DEV_CMD_EFFECT_RESET_FAULT;
    case DEV_CMD_HOME_DEVICE:
        return DEV_CMD_EFFECT_HOME_DEVICE;
    case DEV_CMD_MANUAL_ACTUATOR:
        return DEV_CMD_EFFECT_MANUAL_ACTUATOR;
    case DEV_CMD_STOP_ALL_OUTPUTS:
        return DEV_CMD_EFFECT_STOP_ALL_OUTPUTS;
    case DEV_CMD_ENTER_MANUAL:
    case DEV_CMD_STOP_OPERATION:
    case DEV_CMD_RESUME_OPERATION:
    case DEV_CMD_RECOVER:
        return DEV_CMD_EFFECT_NONE;
    default:
        return DEV_CMD_EFFECT_NONE;
    }
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

    if (kind == DEV_CMD_RESET_FAULT) {
        if ((s_mode == OP_MODE_IDLE) && !alarm_registry_has_blocking_active()
            && (alarm_registry_safety_posture() != SAFETY_POSTURE_LOCKOUT)) {
            return make_denied(OP_REJECT_WRONG_MODE);
        }
    }

    if (kind == DEV_CMD_START_WASH) {
        if (!s_service_enabled) {
            return make_denied(OP_REJECT_SERVICE_DISABLED);
        }
        if (alarm_registry_has_blocking_active()) {
            return make_denied(OP_REJECT_WRONG_MODE);
        }
    }

    if (kind == DEV_CMD_RESUME_OPERATION) {
        if (s_service_enabled) {
            return make_denied(OP_REJECT_WRONG_MODE);
        }
    }

    return (dev_cmd_decision_t){
        .verdict        = OP_CMD_ALLOWED,
        .reason         = OP_REJECT_NONE,
        .pending_effect = effect_for_kind(kind),
    };
}

static bool mode_allows_critical_immediate(void)
{
    return (s_mode != OP_MODE_WASHING) && (s_mode != OP_MODE_RECOVERING);
}

sw_err_t operational_mode_init(void)
{
    s_mode            = OP_MODE_IDLE;
    s_estop_active    = false;
    s_service_enabled = true;
    LOG_INFO("operational_mode: init ok (IDLE)");
    return SW_OK;
}

dev_cmd_decision_t op_mode_handle_command(const dev_cmd_t *cmd)
{
    dev_cmd_decision_t d;
    dev_cmd_kind_t     kind;

    d = check_command(cmd);
    if (d.verdict != OP_CMD_ALLOWED) {
        if (cmd != NULL) {
            publish_cmd_rejected(cmd->body.kind, d.reason);
        }
        return d;
    }

    kind = cmd->body.kind;

    switch (kind) {
    case DEV_CMD_ENTER_MANUAL:
        set_mode(OP_MODE_MANUAL);
        break;

    case DEV_CMD_START_SELF_CHECK:
        set_mode(OP_MODE_SELF_CHECK);
        break;

    case DEV_CMD_RECOVER:
        set_mode(OP_MODE_RECOVERING);
        publish_recovery_requested();
        break;

    case DEV_CMD_STOP_OPERATION:
        op_mode_set_service_enabled(false);
        break;

    case DEV_CMD_RESUME_OPERATION:
        op_mode_set_service_enabled(true);
        break;

    case DEV_CMD_START_WASH:
    case DEV_CMD_STOP_WASH:
    case DEV_CMD_MANUAL_ACTUATOR:
    case DEV_CMD_RESET_FAULT:
    case DEV_CMD_HOME_DEVICE:
    case DEV_CMD_STOP_ALL_OUTPUTS:
        break;

    default:
        d = make_denied(OP_REJECT_UNKNOWN_CMD);
        publish_cmd_rejected(kind, d.reason);
        break;
    }

    return d;
}

void op_mode_on_wash_session_started(void)
{
    if (s_mode == OP_MODE_IDLE) {
        set_mode(OP_MODE_WASHING);
    }
}

void op_mode_on_wash_session_completed(void)
{
    if (s_mode == OP_MODE_WASHING) {
        set_mode(OP_MODE_IDLE);
    }
}

void op_mode_on_wash_session_aborted(wash_abort_cause_t cause)
{
    if (s_mode != OP_MODE_WASHING) {
        return;
    }

    if (cause == WASH_ABORT_MANUAL) {
        set_mode(OP_MODE_IDLE);
    } else {
        set_mode(OP_MODE_EXCEPTION);
    }
}

void op_mode_on_post_wash_assessment(bool enter_exception)
{
    if ((s_mode == OP_MODE_IDLE) && enter_exception) {
        set_mode(OP_MODE_EXCEPTION);
    }
}

void op_mode_on_self_check_completed(bool land_exception)
{
    if (s_mode != OP_MODE_SELF_CHECK) {
        return;
    }

    if (land_exception) {
        set_mode(OP_MODE_EXCEPTION);
    } else {
        set_mode(OP_MODE_IDLE);
    }
}

void op_mode_on_critical_alarm(void)
{
    if (mode_allows_critical_immediate()) {
        set_mode(OP_MODE_EXCEPTION);
    }
}

void op_mode_on_estop_triggered(void)
{
    s_estop_active = true;
    set_mode(OP_MODE_EXCEPTION);
}

void op_mode_on_estop_cleared(void)
{
    s_estop_active = false;
    publish_context_sync();
}

void op_mode_on_recovery_completed(recovery_result_t result)
{
    if (s_mode != OP_MODE_RECOVERING) {
        return;
    }

    if (result == RECOVERY_RESULT_IDLE) {
        set_mode(OP_MODE_IDLE);
    } else {
        set_mode(OP_MODE_EXCEPTION);
    }
}

void op_mode_on_legacy_reset_fault(void)
{
    if (s_mode == OP_MODE_EXCEPTION) {
        set_mode(OP_MODE_IDLE);
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
    if (!s_service_enabled) {
        return true;
    }

    return (s_mode == OP_MODE_INIT) || (s_mode == OP_MODE_EXCEPTION) || (s_mode == OP_MODE_RECOVERING);
}

bool op_mode_is_standby(void)
{
    return (s_mode == OP_MODE_IDLE) && s_service_enabled;
}

void op_mode_set_service_enabled(bool enabled)
{
    s_service_enabled = enabled;
    publish_context_sync();
}
