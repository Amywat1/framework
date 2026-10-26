/**
 * @file    command_submit.c
 * @brief   入站命令提交 helper
 */

#include "application/command_submit.h"

#include "application/ports/inbound/command/command_port.h"
#include "common/log.h"

#include <stdatomic.h>

static atomic_uint_fast64_t s_next_request_id;

void command_submit_prepare(dev_cmd_t *cmd, trace_context_t *previous)
{
    trace_context_t current;

    if ((cmd == NULL) || (previous == NULL)) {
        return;
    }

    *previous = trace_context_get();
    if (cmd->meta.request_id == 0U) {
        cmd->meta.request_id = atomic_fetch_add(&s_next_request_id, 1U) + 1U;
    }
    if (cmd->meta.wash_session_id == 0U) {
        cmd->meta.wash_session_id = previous->wash_session_id;
    }
    if (dev_cmd_is_order_flow(cmd->body.kind) && (cmd->meta.wash_session_id == 0U)) {
        cmd->meta.wash_session_id = cmd->meta.request_id;
    }
    if (cmd->meta.correlation_id == 0U) {
        cmd->meta.correlation_id = (previous->correlation_id != 0U)
                                       ? previous->correlation_id
                                       : cmd->meta.request_id;
    }
    if (cmd->meta.causation_id == 0U) {
        cmd->meta.causation_id = previous->causation_id;
    }

    current.wash_session_id = cmd->meta.wash_session_id;
    current.command_id      = cmd->meta.request_id;
    current.correlation_id  = cmd->meta.correlation_id;
    current.causation_id    = cmd->meta.causation_id;
    trace_context_set(&current);
}

sw_err_t command_submit_dispatch(dev_cmd_t *cmd, dev_cmd_receipt_t *receipt)
{
    const device_command_port_ops_t *cp;

    if (cmd == NULL) {
        return SW_ERR_PARAM;
    }

    cp = device_command_port_get_ops();
    if ((cp == NULL) || (cp->submit_sync == NULL)) {
        LOG_WARN("command_submit: device_command_port not registered");
        if (receipt != NULL) {
            receipt->status       = DEV_CMD_STATUS_FAILED;
            receipt->effect_error = SW_ERR_NOT_INIT;
            receipt->request_id   = cmd->meta.request_id;
        }
        return SW_ERR_NOT_INIT;
    }

    return cp->submit_sync(cmd, receipt, COMMAND_SUBMIT_TIMEOUT_MS);
}

sw_err_t command_submit(dev_cmd_t *cmd)
{
    trace_context_t previous;
    sw_err_t        ret;

    if (cmd == NULL) {
        return SW_ERR_PARAM;
    }

    command_submit_prepare(cmd, &previous);
    ret = command_submit_dispatch(cmd, NULL);
    trace_context_set(&previous);
    return ret;
}

sw_err_t command_submit_kind(dev_cmd_kind_t kind, dev_cmd_source_t source)
{
    dev_cmd_t cmd = dev_cmd_make_simple(kind);

    cmd.meta.source = source;
    return command_submit(&cmd);
}

sw_err_t command_submit_manual(uint32_t act_id, int32_t param, dev_cmd_source_t source)
{
    dev_cmd_t cmd = dev_cmd_make_manual(act_id, param);

    cmd.meta.source = source;
    return command_submit(&cmd);
}

sw_err_t command_submit_set_service(bool enabled, dev_cmd_source_t source)
{
    dev_cmd_t cmd = dev_cmd_make_set_service(enabled);

    cmd.meta.source = source;
    return command_submit(&cmd);
}
